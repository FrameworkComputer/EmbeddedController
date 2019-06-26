/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <string.h>

#include "Global.h"

#include "board_id.h"
#include "console.h"
#include "cryptoc/sha256.h"
#include "link_defs.h"
#include "rma_auth.h"
#include "sn_bits.h"
#include "u2f_impl.h"
#include "virtual_nvmem.h"

/*
 * Functions to allow access to non-NVRam data through NVRam Indexes.
 *
 * These functions map virtual NV indexes to virtual offsets, and allow
 * reads from those virtual offsets. The functions are contrained based on the
 * implementation of the calling TPM functions; these constraints and other
 * assumptions are described below.
 *
 * The TPM NVRam functions make use of the available NVRam space to store NVRam
 * Indexes in a linked list with the following structure:
 *
 * struct nvram_list_node {
 *   UINT32 next_node_offset;
 *   TPM_HANDLE this_node_handle;
 *   NV_INDEX index;
 *   BYTE data[];
 * };
 *
 * The TPM functions for operating on NVRam begin by iterating through the list
 * to find the offset for the relevant Index.
 *
 * See NvFindHandle() in //third_party/tpm2/NV.c for more details.
 *
 * Once the offset has been found, read operations on the NV Index will
 * call _plat__NvMemoryRead() twice, first to read the NV_INDEX data, and
 * second to read the actual NV data.
 *
 * The offset x returned by NvFindHandle() is to the this_node_handle element of
 * the linked list node; the subsequent reads are therefore to
 * x+sizeof(TPM_HANDLE) and x+sizeof(TPM_HANDLE)+sizeof(NV_INDEX).
 *
 * The first read, to retrieve NV_INDEX data, is always a fixed size
 * (sizeof(NV_INDEX)). The size of the second read is user defined, but will
 * not exceed the size of the data.
 */

/*  Size constraints for virtual NV indexes. */
#define VIRTUAL_NV_INDEX_HEADER_SIZE       sizeof(NV_INDEX)
#define MAX_VIRTUAL_NV_INDEX_DATA_SIZE     0x200
#define MAX_VIRTUAL_NV_INDEX_SLOT_SIZE     (sizeof(TPM_HANDLE) +	   \
					    VIRTUAL_NV_INDEX_HEADER_SIZE + \
					    MAX_VIRTUAL_NV_INDEX_DATA_SIZE)

/*
 * Prefix for virtual NV offsets. Chosen such that all virtual NV offsets are
 * not valid memory addresses, to ensure it is impossible to accidentally read
 * (incorrect) virtual NV data from anywhere other than these functions.
 */
#define VIRTUAL_NV_OFFSET_START            0xfff00000
#define VIRTUAL_NV_OFFSET_END		   0xffffffff
/* Used to check if offsets are virtual. */
#define VIRTUAL_NV_OFFSET_MASK		   (~(VIRTUAL_NV_OFFSET_END -     \
					      VIRTUAL_NV_OFFSET_START))

/*
 * These offsets are the two offsets queried by the TPM code, as a result of the
 * design of that code, and the linked list structure described above.
 */
#define NV_INDEX_READ_OFFSET               0x00000004 /* sizeof(uint32_t) */
#define NV_DATA_READ_OFFSET                0x00000098 /* NV_INDEX_READ_OFFSET
						       *   + sizeof(NV_INDEX)
						       */

/* Template for the NV_INDEX data. */
#define NV_INDEX_TEMPLATE {						  \
	.publicArea = {							  \
		.nameAlg = TPM_ALG_SHA256,				  \
		.attributes = {						  \
			/* Allow index to be read using its authValue. */ \
			.TPMA_NV_AUTHREAD = 1,				  \
			/*						  \
			 * The spec requires at least one write		  \
			 * authentication method to be specified. We	  \
			 * intentionally don't include one, so that	  \
			 * this index cannot be spoofed by an		  \
			 * attacker running a version of cr50 that	  \
			 * pre-dates the implementation of virtual	  \
			 * NV indices.					  \
			 * .TPMA_NV_AUTHWRITE = 1,			  \
			 * Only allow deletion if the authPolicy is	  \
			 * satisied. The authPolicy is empty, and so	  \
			 * cannot be satisfied, so this effectively	  \
			 * disables deletion.				  \
			 */						  \
			.TPMA_NV_POLICY_DELETE = 1,			  \
			/* Prevent writes. */				  \
			.TPMA_NV_WRITELOCKED = 1,			  \
			/* Write-lock will not be cleared on startup. */  \
			.TPMA_NV_WRITEDEFINE = 1,			  \
			/* Index has been written, can be read. */	  \
			.TPMA_NV_WRITTEN = 1,				  \
		},							  \
		.authPolicy = { },					  \
	},								  \
	.authValue = { },						  \
};

/* Configuration data for virtual NV indexes. */
struct virtual_nv_index_cfg {
	uint16_t size;

	void (*get_data_fn)(BYTE *to, size_t offset, size_t size);
} __packed;

#define REGISTER_CONFIG(r_index, r_size, r_get_data_fn) \
	[r_index - VIRTUAL_NV_INDEX_START] = { \
		.size = r_size, \
		.get_data_fn = r_get_data_fn \
	},

#define REGISTER_DEPRECATED_CONFIG(r_index) \
	REGISTER_CONFIG(r_index, 0, 0)


/*
 * The salt to be mixed in with RMA device ID to produce RSU device ID.
 */
#define RSU_SALT_SIZE 32
const char kRsuSalt[] = "Wu8oGt0uu0H8uSGxfo75uSDrGcRk2BXh";
BUILD_ASSERT(ARRAY_SIZE(kRsuSalt) == RSU_SALT_SIZE+1);

/*
 * Helpers for dealing with NV indexes, associated configs and offsets.
 */

/*
 * Looks up the config for the specified virtual NV index, and sets a default
 * 'empty' config if the index is not defined.
 */
static inline void GetNvIndexConfig(
	enum virtual_nv_index index, struct virtual_nv_index_cfg *cfg);

/* Converts a virtual NV index to the corresponding virtual offset. */
static inline BOOL NvIndexToNvOffset(uint32_t index)
{
	return VIRTUAL_NV_OFFSET_START +
		((index - VIRTUAL_NV_INDEX_START) *
		 MAX_VIRTUAL_NV_INDEX_SLOT_SIZE);
}

/* Converts an virtual offset to the corresponding NV Index. */
static inline BOOL NvOffsetToNvIndex(uint32_t offset)
{
	return VIRTUAL_NV_INDEX_START +
		((offset - VIRTUAL_NV_OFFSET_START) /
		 MAX_VIRTUAL_NV_INDEX_SLOT_SIZE);
}

/*
 * Copies the template NV_INDEX data to the specified destination, and updates
 * it with the specified NV index and size values.
 */
static inline void CopyNvIndex(void *dest, size_t start, size_t count,
			       uint32_t nvIndex, uint32_t size)
{
	NV_INDEX nv_index_template = NV_INDEX_TEMPLATE;

	nv_index_template.publicArea.nvIndex = nvIndex;
	nv_index_template.publicArea.dataSize = size;
	memcpy(dest, ((BYTE *) &nv_index_template) + start, count);
}

/*
 * Functions exposed to the TPM2 code.
 */

uint32_t _plat__NvGetHandleVirtualOffset(uint32_t handle)
{
	if (handle >= VIRTUAL_NV_INDEX_START && handle <= VIRTUAL_NV_INDEX_MAX)
		return NvIndexToNvOffset(handle);
	else
		return 0;
}

BOOL _plat__NvOffsetIsVirtual(unsigned int startOffset)
{
	return (startOffset & VIRTUAL_NV_OFFSET_MASK) ==
			VIRTUAL_NV_OFFSET_START;
}

void _plat__NvVirtualMemoryRead(unsigned int startOffset, unsigned int size,
				void *data)
{
	uint32_t nvIndex;
	struct virtual_nv_index_cfg nvIndexConfig;
	unsigned int offset;

	nvIndex = NvOffsetToNvIndex(startOffset);
	GetNvIndexConfig(nvIndex, &nvIndexConfig);

	/* Calculate offset within this section. */
	startOffset = startOffset - NvIndexToNvOffset(nvIndex);

	offset = startOffset;
	while (size > 0) {
		int section_offset;
		int copied;

		if (offset < NV_INDEX_READ_OFFSET) {
			/*
			 * The first 4 bytes are supposed to represent a pointer
			 * to the next element in the NV index list; we don't
			 * have a next item, so return 0.
			 */
			copied = MIN(sizeof(TPM_HANDLE) - offset, size);

			memset((BYTE *) data, 0, copied);
		} else if (offset < NV_DATA_READ_OFFSET) {
			/*
			 * The NV_INDEX section is the second section, which
			 * immediately folows the 'next' pointer above.
			 */
			section_offset = offset - NV_INDEX_READ_OFFSET;
			copied = MIN(VIRTUAL_NV_INDEX_HEADER_SIZE -
				     section_offset, size);

			CopyNvIndex((BYTE *)data + offset - startOffset,
				    section_offset,
				    copied,
				    nvIndex, nvIndexConfig.size);
		} else if (offset < NV_DATA_READ_OFFSET + nvIndexConfig.size) {
			/*
			 * The actual NV data is the final section, which
			 * immediately follos the NV_INDEX.
			 */
			section_offset = offset - NV_DATA_READ_OFFSET;
			copied = MIN(nvIndexConfig.size - section_offset, size);

			nvIndexConfig.get_data_fn((BYTE *)data + offset -
						  startOffset,
						  section_offset,
						  copied);
		} else {
			/* More data was requested than is available. */
#ifdef CR50_DEV
			cprints(CC_TPM,
				"Invalid vNVRAM read, offset: %x, size: %x",
				offset, size);
#endif
			memset((BYTE *)data + offset - startOffset, 0,
			       size);
			break;
		}

		offset += copied;
		size -= copied;
	}
}

/*
 *  Helpers to fetch actual virtual NV data.
 */

static void GetBoardId(BYTE *to, size_t offset, size_t size)
{
	struct board_id board_id_tmp;

	read_board_id(&board_id_tmp);
	memcpy(to, ((BYTE *) &board_id_tmp) + offset, size);
}
BUILD_ASSERT(VIRTUAL_NV_INDEX_BOARD_ID_SIZE == sizeof(struct board_id));

static void GetSnData(BYTE *to, size_t offset, size_t size)
{
	struct sn_data sn_data_tmp;

	read_sn_data(&sn_data_tmp);
	memcpy(to, ((BYTE *) &sn_data_tmp) + offset, size);
}
BUILD_ASSERT(VIRTUAL_NV_INDEX_SN_DATA_SIZE == sizeof(struct sn_data));

static void GetG2fCert(BYTE *to, size_t offset, size_t size)
{
	uint8_t cert[G2F_ATTESTATION_CERT_MAX_LEN] = { 0 };

	if (!g2f_attestation_cert(cert))
		memset(cert, 0, G2F_ATTESTATION_CERT_MAX_LEN);

	memcpy(to, ((BYTE *) cert) + offset, size);
}
BUILD_ASSERT(VIRTUAL_NV_INDEX_G2F_CERT_SIZE == G2F_ATTESTATION_CERT_MAX_LEN);

static void GetRSUDevID(BYTE *to, size_t offset, size_t size)
{
	LITE_SHA256_CTX ctx;
	uint8_t rma_device_id[RMA_DEVICE_ID_SIZE];
	const uint8_t *rsu_device_id;

	get_rma_device_id(rma_device_id);

	SHA256_init(&ctx);
	HASH_update(&ctx, rma_device_id, sizeof(rma_device_id));
	HASH_update(&ctx, kRsuSalt, RSU_SALT_SIZE);
	rsu_device_id = HASH_final(&ctx);

	memcpy(to, rsu_device_id + offset, size);
}
BUILD_ASSERT(VIRTUAL_NV_INDEX_RSU_DEV_ID_SIZE == SHA256_DIGEST_SIZE);

/*
 * Registration of current virtual indexes.
 *
 * Indexes are declared in the virtual_nv_index enum in the header.
 *
 * Active entries of this enum must have a size and data function registered
 * with a REGISTER_CONFIG statement below.
 *
 * Deprecated indices should use the REGISTER_DEPRECATED_CONFIG variant.
 */

static const struct virtual_nv_index_cfg index_config[] = {
	REGISTER_CONFIG(VIRTUAL_NV_INDEX_BOARD_ID,
			VIRTUAL_NV_INDEX_BOARD_ID_SIZE,
			GetBoardId)
	REGISTER_CONFIG(VIRTUAL_NV_INDEX_SN_DATA,
			VIRTUAL_NV_INDEX_SN_DATA_SIZE,
			GetSnData)
	REGISTER_CONFIG(VIRTUAL_NV_INDEX_G2F_CERT,
			VIRTUAL_NV_INDEX_G2F_CERT_SIZE,
			GetG2fCert)
	REGISTER_CONFIG(VIRTUAL_NV_INDEX_RSU_DEV_ID,
			VIRTUAL_NV_INDEX_RSU_DEV_ID_SIZE,
			GetRSUDevID)
};

/* Check sanity of above config. */
BUILD_ASSERT(VIRTUAL_NV_INDEX_END <= (VIRTUAL_NV_INDEX_MAX + 1));
BUILD_ASSERT((VIRTUAL_NV_INDEX_END - VIRTUAL_NV_INDEX_START) ==
	     ARRAY_SIZE(index_config));
/* Check we will never overrun the virtual address space. */
BUILD_ASSERT((VIRTUAL_NV_INDEX_MAX - VIRTUAL_NV_INDEX_START + 1) *
	     MAX_VIRTUAL_NV_INDEX_SLOT_SIZE <
	     (VIRTUAL_NV_OFFSET_END - VIRTUAL_NV_OFFSET_START));

static inline void GetNvIndexConfig(
	enum virtual_nv_index index, struct virtual_nv_index_cfg *cfg)
{
	if (index >= VIRTUAL_NV_INDEX_START && index < VIRTUAL_NV_INDEX_END) {
		*cfg = index_config[index - VIRTUAL_NV_INDEX_START];
	} else {
		cfg->size = 0;
		cfg->get_data_fn = 0;
	}
}
