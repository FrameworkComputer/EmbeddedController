/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "dcrypto.h"
#include "internal.h"
#include "endian.h"
#include "registers.h"

void DCRYPTO_ladder_init(void)
{
	/* Do not reset keyladder engine here, as before.
	 *
	 * Should not be needed and if it is, it is indicative
	 * of sync error between this and sha engine usage.
	 * Reset will make this flow work, but will have broken
	 * the other pending sha flow.
	 * Hence leave as is and observe the error.
	 *
	 * TODO: hw sha engine usage and keyladder usage cannot
	 * interleave and should share a semaphore.
	 */
}

int DCRYPTO_ladder_step(uint32_t cert)
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

static int compute_certs(const uint32_t *certs, size_t num_certs)
{
	int i;

	for (i = 0; i < num_certs; i++) {
		if (DCRYPTO_ladder_step(certs[i]))
			return 0;
	}

	return 1;
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

static const uint32_t FRK2_CERTS_PREFIX[] = {
	KEYMGR_CERT_0,
	KEYMGR_CERT_3,
	KEYMGR_CERT_4,
	KEYMGR_CERT_5,
	KEYMGR_CERT_7,
	KEYMGR_CERT_15,
	KEYMGR_CERT_20,
};

static const uint32_t FRK2_CERTS_POSTFIX[] = {
	KEYMGR_CERT_26,
};

#define MAX_MAJOR_FW_VERSION 254

int DCRYPTO_ladder_compute_frk2(size_t fw_version, uint8_t *frk2)
{
	int i;

	if (fw_version > MAX_MAJOR_FW_VERSION)
		return 0;

	DCRYPTO_ladder_init();

	if (!compute_certs(FRK2_CERTS_PREFIX, ARRAY_SIZE(FRK2_CERTS_PREFIX)))
		return 0;

	for (i = 0; i < MAX_MAJOR_FW_VERSION - fw_version; i++) {
		if (DCRYPTO_ladder_step(KEYMGR_CERT_25))
			return 0;
	}

	if (!compute_certs(FRK2_CERTS_POSTFIX, ARRAY_SIZE(FRK2_CERTS_POSTFIX)))
		return 0;

	memcpy(frk2, (void *) GREG32_ADDR(KEYMGR, HKEY_FRR0),
		AES256_BLOCK_CIPHER_KEY_SIZE);
	return 1;
}
