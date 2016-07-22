/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "tpm_manufacture.h"
#include "tpm_registers.h"

#include "TPM_Types.h"
#include "TpmBuildSwitches.h"
#include "CryptoEngine.h"
#include "CpriECC_fp.h"
#include "CpriRSA_fp.h"
#include "tpm_types.h"

#include "Global.h"
#include "Hierarchy_fp.h"
#include "InternalRoutines.h"
#include "Manufacture_fp.h"
#include "NV_Write_fp.h"
#include "NV_DefineSpace_fp.h"

#include "console.h"
#include "extension.h"
#include "flash.h"
#include "flash_config.h"
#include "flash_info.h"
#include "printf.h"
#include "registers.h"

#include "dcrypto.h"

#include <cryptoc/sha256.h>

#include <endian.h>
#include <string.h>

#define CPRINTF(format, args...) cprintf(CC_EXTENSION, format, ## args)

#define EK_CERT_NV_START_INDEX             0x01C00000
#define INFO1_EPS_SIZE                     PRIMARY_SEED_SIZE
#define INFO1_EPS_OFFSET                   FLASH_INFO_MANUFACTURE_STATE_OFFSET
#define AES256_BLOCK_CIPHER_KEY_SIZE       32

#define RO_CERTS_START_ADDR                 0x43800
#define RO_CERTS_REGION_SIZE                0x0800

enum cros_perso_component_type {
	CROS_PERSO_COMPONENT_TYPE_EPS       = 128,
	CROS_PERSO_COMPONENT_TYPE_RSA_CERT  = 129,
	CROS_PERSO_COMPONENT_TYPE_P256_CERT = 130
};

struct cros_perso_response_component_info_v0 {
	uint16_t component_size;
	uint8_t  component_type;
	uint8_t  reserved[5];
} __packed;                                             /* Size: 8B */

/* key_id: key for which this is the certificate */
/* cert_len: length of the following certificate */
/* cert: the certificate bytes */
struct cros_perso_certificate_response_v0 {
	uint8_t key_id[4];
	uint32_t cert_len;
	uint8_t cert[0];
} __packed;                                             /* Size: 8B */

/* Personalization response. */
BUILD_ASSERT(sizeof(struct cros_perso_response_component_info_v0) == 8);
BUILD_ASSERT(sizeof(struct cros_perso_certificate_response_v0) == 8);

/* TODO(ngm): replace with real pub key. */
static const uint32_t TEST_ENDORSEMENT_CA_RSA_N[64] = {
	0xfa3b34ed, 0x3c59ad05, 0x912d6623, 0x83302402,
	0xd43b6755, 0x5777021a, 0xaf37e9a1, 0x45c0e8ad,
	0x9728f946, 0x4391523d, 0xdf7a9164, 0x88f1a9ae,
	0x036c557e, 0x5d9df43e, 0x3e65de68, 0xe172008a,
	0x709dc81f, 0x27a75fe0, 0x3e77f89e, 0x4f400ecc,
	0x51a17dae, 0x2ff9c652, 0xd1d83cdb, 0x20d26349,
	0xbbad71dd, 0x30051b2b, 0x276b2459, 0x809bb8e1,
	0xb8737049, 0xdbe94466, 0x8287072b, 0x070ef311,
	0x6e2a26de, 0x29d69f11, 0x96463d95, 0xb4dc6950,
	0x097d4dfe, 0x1b4a88cc, 0xbd6b50c8, 0x9f7a5b34,
	0xda22c199, 0x9d1ac04b, 0x136af5e5, 0xb1a0e824,
	0x4a065b34, 0x1f67fb46, 0xa1f91ab1, 0x27bb769f,
	0xb704c992, 0xb669cbf4, 0x9299bb6c, 0xcb1b2208,
	0x2dc0d9db, 0xe1513e13, 0xc7f24923, 0xa74c6bcc,
	0xca1a9a69, 0x1b994244, 0x4f64b0d9, 0x78607fd6,
	0x486fb315, 0xa1098c31, 0x5dc50dd6, 0xcdc10874
};

static const struct RSA TEST_ENDORSEMENT_CA_RSA_PUB = {
	.e = RSA_F4,
	.N = {
		.dmax = sizeof(TEST_ENDORSEMENT_CA_RSA_N) / sizeof(uint32_t),
		.d = (struct access_helper *) TEST_ENDORSEMENT_CA_RSA_N,
	},
	.d = {
		.dmax = 0,
		.d = NULL,
	},
};

static int validate_cert(
	const struct cros_perso_response_component_info_v0 *cert_info,
	const struct cros_perso_certificate_response_v0 *cert,
	const uint8_t eps[PRIMARY_SEED_SIZE])
{
	if (cert_info->component_type != CROS_PERSO_COMPONENT_TYPE_RSA_CERT &&
	    cert_info->component_type !=
	    CROS_PERSO_COMPONENT_TYPE_P256_CERT)
		return 0;  /* Invalid component type. */

	/* TODO(ngm): verify key_id against HIK/FRK0. */
	if (cert->cert_len > MAX_NV_BUFFER_SIZE)
		return 0;

	/* Verify certificate signature. */
	return DCRYPTO_x509_verify(cert->cert, cert->cert_len,
				&TEST_ENDORSEMENT_CA_RSA_PUB);
}

static int store_cert(enum cros_perso_component_type component_type,
		const struct cros_perso_certificate_response_v0 *cert)
{
	const uint32_t rsa_ek_nv_index = EK_CERT_NV_START_INDEX;
	const uint32_t ecc_ek_nv_index = EK_CERT_NV_START_INDEX + 1;
	uint32_t nv_index;
	NV_DefineSpace_In define_space;
	TPMA_NV space_attributes;
	NV_Write_In in;

	/* Clear up structures potentially uszed only partially. */
	memset(&define_space, 0, sizeof(define_space));
	memset(&space_attributes, 0, sizeof(space_attributes));
	memset(&in, 0, sizeof(in));

	/* Indicate that a system reset has occurred, and currently
	 * running with Platform auth.
	 */
	HierarchyStartup(SU_RESET);

	if (component_type == CROS_PERSO_COMPONENT_TYPE_RSA_CERT)
		nv_index = rsa_ek_nv_index;
	else   /* P256 certificate. */
		nv_index = ecc_ek_nv_index;

	/* EK Credential attributes specified in the "TCG PC Client
	 * Platform, TPM Profile (PTP) Specification" document.
	 */
	/* REQUIRED: Writeable under platform auth. */
	space_attributes.TPMA_NV_PPWRITE = 1;
	/* OPTIONAL: Write-once; space must be deleted to be re-written. */
	space_attributes.TPMA_NV_WRITEDEFINE = 1;
	/* REQUIRED: Space created with platform auth. */
	space_attributes.TPMA_NV_PLATFORMCREATE = 1;
	/* REQUIRED: Readable under empty password? */
	space_attributes.TPMA_NV_AUTHREAD = 1;
	/* REQUIRED: Disable dictionary attack protection. */
	space_attributes.TPMA_NV_NO_DA = 1;

	define_space.authHandle = TPM_RH_PLATFORM;
	define_space.auth.t.size = 0;
	define_space.publicInfo.t.size = sizeof(
		define_space.publicInfo.t.nvPublic);
	define_space.publicInfo.t.nvPublic.nvIndex = nv_index;
	define_space.publicInfo.t.nvPublic.nameAlg = TPM_ALG_SHA256;
	define_space.publicInfo.t.nvPublic.attributes = space_attributes;
	define_space.publicInfo.t.nvPublic.authPolicy.t.size = 0;
	define_space.publicInfo.t.nvPublic.dataSize = cert->cert_len;

	/* Define the required space first. */
	if (TPM2_NV_DefineSpace(&define_space) != TPM_RC_SUCCESS)
		return 0;

	/* TODO(ngm): call TPM2_NV_WriteLock(nvIndex) on tpm_init();
	 * this prevents delete?
	 */

	in.nvIndex = nv_index;
	in.authHandle = TPM_RH_PLATFORM;
	in.data.t.size = cert->cert_len;
	memcpy(in.data.t.buffer, cert->cert, cert->cert_len);
	in.offset = 0;

	if (TPM2_NV_Write(&in) != TPM_RC_SUCCESS)
		return 0;
	if (NvCommit())
		return 1;
	return 0;
}

static uint32_t hw_key_ladder_step(uint32_t cert)
{
	uint32_t itop;

	GREG32(KEYMGR, SHA_ITOP) = 0;  /* clear status */

	GREG32(KEYMGR, SHA_USE_CERT_INDEX) =
		(cert << GC_KEYMGR_SHA_USE_CERT_INDEX_LSB) |
		GC_KEYMGR_SHA_USE_CERT_ENABLE_MASK;

	GREG32(KEYMGR, SHA_CFG_EN) =
		GC_KEYMGR_SHA_CFG_EN_INT_EN_DONE_MASK;
	GREG32(KEYMGR, SHA_TRIG) =
		GC_KEYMGR_SHA_TRIG_TRIG_GO_MASK;

	do {
		itop = GREG32(KEYMGR, SHA_ITOP);
	} while (!itop);

	GREG32(KEYMGR, SHA_ITOP) = 0;  /* clear status */

	return !!GREG32(KEYMGR, HKEY_ERR_FLAGS);
}


#define KEYMGR_CERT_0 0
#define KEYMGR_CERT_3 3
#define KEYMGR_CERT_4 4
#define KEYMGR_CERT_5 5
#define KEYMGR_CERT_7 7
#define KEYMGR_CERT_15 15
#define KEYMGR_CERT_20 20
#define KEYMGR_CERT_25 25
#define KEYMGR_CERT_26 26

#define K_CROS_FW_MAJOR_VERSION 0
static const uint8_t k_cr50_max_fw_major_version = 254;

static int compute_frk2(uint8_t frk2[AES256_BLOCK_CIPHER_KEY_SIZE])
{
	int i;

	/* TODO(ngm): reading ITOP in hw_key_ladder_step hangs on
	 * second run of this function (i.e. install of ECC cert,
	 * which re-generates FRK2) unless the SHA engine is reset.
	 */
	GREG32(KEYMGR, SHA_TRIG) =
		GC_KEYMGR_SHA_TRIG_TRIG_RESET_MASK;

	if (hw_key_ladder_step(KEYMGR_CERT_0))
		return 0;

	/* Derive HC_PHIK --> Deposited into ISR0 */
	if (hw_key_ladder_step(KEYMGR_CERT_3))
		return 0;

	/* Cryptographically mix OBS-FBS --> Deposited into ISR1 */
	if (hw_key_ladder_step(KEYMGR_CERT_4))
		return 0;

	/* Derive HIK_RT --> Deposited into ISR0 */
	if (hw_key_ladder_step(KEYMGR_CERT_5))
		return 0;

	/* Derive BL_HIK --> Deposited into ISR0 */
	if (hw_key_ladder_step(KEYMGR_CERT_7))
		return 0;

	/* Generate FRK2 by executing certs 15, 20, 25, and 26 */
	if (hw_key_ladder_step(KEYMGR_CERT_15))
		return 0;

	if (hw_key_ladder_step(KEYMGR_CERT_20))
		return 0;

	for (i = 0; i < k_cr50_max_fw_major_version -
			K_CROS_FW_MAJOR_VERSION; i++) {
		if (hw_key_ladder_step(KEYMGR_CERT_25))
			return 0;
	}
	if (hw_key_ladder_step(KEYMGR_CERT_26))
		return 0;
	memcpy(frk2, (void *) GREG32_ADDR(KEYMGR, HKEY_FRR0),
		AES256_BLOCK_CIPHER_KEY_SIZE);
	return 1;
}

static void flash_info_read_enable(void)
{
	/* Enable R access to INFO. */
	GREG32(GLOBALSEC, FLASH_REGION7_BASE_ADDR) = FLASH_INFO_MEMORY_BASE +
		FLASH_INFO_MANUFACTURE_STATE_OFFSET;
	GREG32(GLOBALSEC, FLASH_REGION7_SIZE) =
		FLASH_INFO_MANUFACTURE_STATE_SIZE - 1;
	GREG32(GLOBALSEC, FLASH_REGION7_CTRL) =
		GC_GLOBALSEC_FLASH_REGION7_CTRL_EN_MASK |
		GC_GLOBALSEC_FLASH_REGION7_CTRL_RD_EN_MASK;
}

static void flash_info_read_disable(void)
{
	GREG32(GLOBALSEC, FLASH_REGION7_CTRL) = 0;
}

static void flash_cert_region_enable(void)
{
	/* Enable R access to CERT block. */
	GREG32(GLOBALSEC, FLASH_REGION6_BASE_ADDR) = RO_CERTS_START_ADDR;
	GREG32(GLOBALSEC, FLASH_REGION6_SIZE) =
		RO_CERTS_REGION_SIZE - 1;
	GREG32(GLOBALSEC, FLASH_REGION6_CTRL) =
		GC_GLOBALSEC_FLASH_REGION6_CTRL_EN_MASK |
		GC_GLOBALSEC_FLASH_REGION6_CTRL_RD_EN_MASK;
}

/* EPS is stored XOR'd with FRK2, so make sure that the sizes match. */
BUILD_ASSERT(AES256_BLOCK_CIPHER_KEY_SIZE == PRIMARY_SEED_SIZE);
static int get_decrypted_eps(uint8_t eps[PRIMARY_SEED_SIZE])
{
	int i;
	uint8_t frk2[AES256_BLOCK_CIPHER_KEY_SIZE];

	CPRINTF("%s: getting eps\n", __func__);
	if (!compute_frk2(frk2))
		return 0;

	/* Setup flash region mapping. */
	flash_info_read_enable();

	for (i = 0; i < INFO1_EPS_SIZE; i += sizeof(uint32_t)) {
		uint32_t word;

		if (flash_physical_info_read_word(
				INFO1_EPS_OFFSET + i, &word) != EC_SUCCESS) {
			memset(frk2, 0, sizeof(frk2));
			return 0;     /* Flash read INFO1 failed. */
		}
		memcpy(eps + i, &word, sizeof(word));
	}

	/* Remove flash region mapping. */
	flash_info_read_disable();

	/* One-time-pad decrypt EPS. */
	for (i = 0; i < PRIMARY_SEED_SIZE; i++)
		eps[i] ^= frk2[i];

	memset(frk2, 0, sizeof(frk2));
	return 1;
}

static int store_eps(const uint8_t eps[PRIMARY_SEED_SIZE])
{
	/* gp is a TPM global state structure, declared in Global.h. */
	memcpy(gp.EPSeed.t.buffer, eps, PRIMARY_SEED_SIZE);

	/* Persist the seed to flash. */
	NvWriteReserved(NV_EP_SEED, &gp.EPSeed);
	return NvCommit();
}

static void endorsement_complete(void)
{
	CPRINTF("%s(): SUCCESS\n", __func__);
}

static int handle_cert(
	const struct cros_perso_response_component_info_v0 *cert_info,
	const struct cros_perso_certificate_response_v0 *cert,
	const uint8_t *eps)
{

	/* Write RSA / P256 endorsement certificate. */
	if (!validate_cert(cert_info, cert, eps))
		return 0;

	/* TODO(ngm): verify that storage succeeded. */
	if (!store_cert(cert_info->component_type, cert)) {
		CPRINTF("%s(): cert storage failed, type: %d\n", __func__,
			cert_info->component_type);
		return 0;  /* Internal failure. */
	}

	return 1;
}

int tpm_endorse(void)
{
	struct ro_cert_response {
		uint8_t key_id[4];
		uint32_t cert_len;
		uint8_t cert[0];
	} __packed;

	struct ro_cert {
		const struct cros_perso_response_component_info_v0 cert_info;
		const struct ro_cert_response cert_response;
	} __packed;

	/* 2-kB RO cert region is setup like so:
	 *
	 *   | struct ro_cert | rsa_cert | struct ro_cert | ecc_cert |
	 */
	const uint8_t *p = (const uint8_t *) RO_CERTS_START_ADDR;
	const uint32_t *c = (const uint32_t *) RO_CERTS_START_ADDR;
	const struct ro_cert *rsa_cert;
	const struct ro_cert *ecc_cert;
	int result = 0;
	uint8_t eps[PRIMARY_SEED_SIZE];

	flash_cert_region_enable();

	/* First boot, certs not yet installed. */
	if (*c == 0xFFFFFFFF)
		return 0;

	if (!get_decrypted_eps(eps)) {
		CPRINTF("%s(): failed to read eps\n", __func__);
		return 0;
	}

	/* Unpack rsa cert struct. */
	rsa_cert = (const struct ro_cert *) p;
	/* Sanity check cert region contents. */
	if ((2 * sizeof(struct ro_cert)) +
		rsa_cert->cert_response.cert_len > RO_CERTS_REGION_SIZE)
		return 0;

	/* Unpack ecc cert struct. */
	ecc_cert = (const struct ro_cert *) (p + sizeof(struct ro_cert) +
					rsa_cert->cert_response.cert_len);
	/* Sanity check cert region contents. */
	if ((2 * sizeof(struct ro_cert)) +
		rsa_cert->cert_response.cert_len +
		ecc_cert->cert_response.cert_len > RO_CERTS_REGION_SIZE)
		return 0;

	/* Verify expected component types. */
	if (rsa_cert->cert_info.component_type !=
		CROS_PERSO_COMPONENT_TYPE_RSA_CERT) {
		return 0;
	}
	if (ecc_cert->cert_info.component_type !=
		CROS_PERSO_COMPONENT_TYPE_P256_CERT) {
		return 0;
	}

	do {
		if (!handle_cert(
				&rsa_cert->cert_info,
				(struct cros_perso_certificate_response_v0 *)
				&rsa_cert->cert_response, eps)) {
			CPRINTF("%s: Failed to process RSA cert\n", __func__);
			break;
		}
		CPRINTF("%s: RSA cert install success\n", __func__);

		if (!handle_cert(
				&ecc_cert->cert_info,
				(struct cros_perso_certificate_response_v0 *)
				&ecc_cert->cert_response, eps)) {
			CPRINTF("%s: Failed to process ECC cert\n", __func__);
			break;
		}
		CPRINTF("%s: ECC cert intsall success\n", __func__);

		/* Copy EPS from INFO1 to flash data region. */
		if (!store_eps(eps)) {
			CPRINTF("%s(): eps storage failed\n", __func__);
			break;
		}

		/* Mark as endorsed. */
		endorsement_complete();

		/* Chip has been marked as manufactured. */
		result = 1;
	} while (0);

	memset(eps, 0, sizeof(eps));
	return result;
}
