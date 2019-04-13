/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* Stuff from tpm2 directory. */
#define HIDE_EC_STDLIB
#define NV_C
#include "Global.h"
#undef NV_C
#include "NV_fp.h"
#include "tpm_generated.h"

#include "nvmem.h"
#include "util.h"

#define NVMEM_CR50_SIZE 272

#ifndef TEST_FUZZ
uint32_t nvmem_user_sizes[NVMEM_NUM_USERS] = {MOCK_NV_MEMORY_SIZE,
					      NVMEM_CR50_SIZE};
#endif

uint32_t s_evictNvStart;
uint32_t s_evictNvEnd;

/* Calculate size of TPM NVMEM. */
#define MOCK_NV_MEMORY_SIZE                                                    \
	(NVMEM_PARTITION_SIZE - sizeof(struct nvmem_tag) - NVMEM_CR50_SIZE)

/*
 * Sizes of the reserved objects stored in the TPM NVMEM. Note that the second
 * last object is in fact a variable size field starting with 4 bytes of size
 * and then up to 512 bytes of actual index data. The array below assumes that
 * the full 512 bytes of the index space are used.
 */
const uint16_t res_sizes[] = {4,  2,  2,  2,  66,   66,	 66,  66, 66,  66,
			      34, 34, 34, 66, 66,   66,	 8,   4,  134, 28,
			      3,  4,  4,  4,  4,    4,	 2,   15, 2,   8,
			      4,  4,  4,  96, 2844, 424, 516, 8};

static uint16_t res_addrs[ARRAY_SIZE(res_sizes)];

BOOL NvEarlyStageFindHandle(TPM_HANDLE handle)
{
	size_t i;

	res_addrs[0] = 0;

	for (i = 1; i < ARRAY_SIZE(res_addrs); i++)
		res_addrs[i] = res_addrs[i - 1] + res_sizes[i - 1];

	s_evictNvStart = res_addrs[i - 1] + res_sizes[i - 1];

	s_evictNvEnd = MOCK_NV_MEMORY_SIZE;
	return 0;
}

void NvGetReserved(UINT32 index, NV_RESERVED_ITEM *ri)
{
	if (index < ARRAY_SIZE(res_sizes)) {
		ri->size = res_sizes[index];
		ri->offset = res_addrs[index];
	} else {
		ri->size = 0;
	}
}

UINT16 UINT16_Marshal(UINT16 *source, BYTE **buffer, INT32 *size)
{
	uint16_t value;

	if (!size || (*size < sizeof(value)))
		return 0;

	value = htobe16(*source);

	memcpy(*buffer, &value, sizeof(value));
	*buffer += sizeof(value);
	*size -= sizeof(value);

	return sizeof(value);
}

UINT16 UINT32_Marshal(UINT32 *source, BYTE **buffer, INT32 *size)
{
	uint32_t value;

	if (!size || (*size < sizeof(value)))
		return 0;

	value = htobe32(*source);

	memcpy(*buffer, &value, sizeof(value));
	*buffer += sizeof(value);
	*size -= sizeof(value);

	return sizeof(value);
}

UINT16 UINT64_Marshal(UINT64 *source, BYTE **buffer, INT32 *size)
{
	uint64_t value;

	if (!size || (*size < sizeof(value)))
		return 0;

	value = htobe64(*source);

	memcpy(*buffer, &value, sizeof(value));
	*buffer += sizeof(value);
	*size -= sizeof(value);

	return sizeof(value);
}

UINT16 TPM2B_DIGEST_Marshal(TPM2B_DIGEST *source, BYTE **buffer, INT32 *size)
{
	UINT16 total_size;
	INT32 i;
	uint8_t *p;

	total_size = UINT16_Marshal(&source->t.size, buffer, size);
	p = *buffer;

	for (i = 0; (i < source->t.size) && *size; ++i) {
		*p++ = source->t.buffer[i];
		*size -= 1;
	}

	total_size += i;
	*buffer = p;

	return total_size;
}

uint16_t TPM2B_AUTH_Marshal(TPM2B_AUTH *source, BYTE **buffer, INT32 *size)
{
	return TPM2B_DIGEST_Marshal(source, buffer, size);
}

uint16_t TPM2B_NONCE_Marshal(TPM2B_AUTH *source, BYTE **buffer, INT32 *size)
{
	return TPM2B_DIGEST_Marshal(source, buffer, size);
}

TPM_RC UINT16_Unmarshal(UINT16 *target, BYTE **buffer, INT32 *size)
{
	uint16_t value;

	if (!size || *size < sizeof(value))
		return TPM_RC_INSUFFICIENT;

	memcpy(&value, *buffer, sizeof(value));
	*target = be16toh(value);

	*buffer += sizeof(value);
	*size -= sizeof(value);

	return TPM_RC_SUCCESS;
}

TPM_RC UINT32_Unmarshal(UINT32 *target, BYTE **buffer, INT32 *size)
{
	uint32_t value;

	if (!size || *size < sizeof(value))
		return TPM_RC_INSUFFICIENT;

	memcpy(&value, *buffer, sizeof(value));
	*target = be32toh(value);

	*buffer += sizeof(value);
	*size -= sizeof(value);

	return TPM_RC_SUCCESS;
}

TPM_RC UINT64_Unmarshal(UINT64 *target, BYTE **buffer, INT32 *size)
{
	uint64_t value;

	if (!size || *size < sizeof(value))
		return TPM_RC_INSUFFICIENT;

	memcpy(&value, *buffer, sizeof(value));
	*target = be64toh(value);

	*buffer += sizeof(value);
	*size -= sizeof(value);

	return TPM_RC_SUCCESS;
}

TPM_RC TPM2B_DIGEST_Unmarshal(TPM2B_DIGEST *target, BYTE **buffer, INT32 *size)
{
	TPM_RC result;
	INT32 i;
	uint8_t *p;

	result = UINT16_Unmarshal(&target->t.size, buffer, size);

	if (result != TPM_RC_SUCCESS)
		return result;

	if (target->t.size == 0)
		return TPM_RC_SUCCESS;

	if ((target->t.size > sizeof(TPMU_HA)) || (target->t.size > *size))
		return TPM_RC_SIZE;

	p = *buffer;
	for (i = 0; i < target->t.size; ++i)
		target->t.buffer[i] = *p++;

	*buffer = p;
	*size -= i;

	return TPM_RC_SUCCESS;
}

TPM_RC TPM2B_AUTH_Unmarshal(TPM2B_AUTH *target, BYTE **buffer, INT32 *size)
{
	return TPM2B_DIGEST_Unmarshal(target, buffer, size);
}

TPM_RC TPM2B_NONCE_Unmarshal(TPM2B_AUTH *target, BYTE **buffer, INT32 *size)
{
	return TPM2B_DIGEST_Unmarshal(target, buffer, size);
}
