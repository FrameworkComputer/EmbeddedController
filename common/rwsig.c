/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * Implementation of the RW firmware signature verification and jump.
 */

#include "console.h"
#include "ec_commands.h"
#include "rsa.h"
#include "sha256.h"
#include "shared_mem.h"
#include "system.h"
#include "usb_pd.h"
#include "util.h"

/* Console output macros */
#define CPRINTF(format, args...) cprintf(CC_SYSTEM, format, ## args)
#define CPRINTS(format, args...) cprints(CC_SYSTEM, format, ## args)

/* Insert the RSA public key definition */
const struct rsa_public_key pkey __attribute__((section(".rsa_pubkey"))) =
#include "gen_pub_key.h"

/* The RSA signature is stored at the end of the RW firmware */
static const void *rw_sig = (void *)CONFIG_FLASH_BASE + CONFIG_FW_RW_OFF
				 + CONFIG_FW_RW_SIZE - RSANUMBYTES;

/* RW firmware reset vector */
static uint32_t * const rw_rst =
	(uint32_t *)(CONFIG_FLASH_BASE+CONFIG_FW_RW_OFF+4);

void check_rw_signature(void)
{
	struct sha256_ctx ctx;
	int good, res;
	uint8_t *hash;
	uint32_t *rsa_workbuf;

	/* Only the Read-Only firmware needs to do the signature check */
	if (system_get_image_copy() != SYSTEM_IMAGE_RO)
		return;

	/* Check if we have a RW firmware flashed */
	if (*rw_rst == 0xffffffff)
		return;

	/* Large buffer for RSA computation : could be re-use afterwards... */
	res = shared_mem_acquire(3 * RSANUMBYTES, (char **)&rsa_workbuf);
	if (res) {
		CPRINTS("No memory for RW verification");
		return;
	}

	/* SHA-256 Hash of the RW firmware */
	SHA256_init(&ctx);
	SHA256_update(&ctx, (void *)CONFIG_FLASH_BASE + CONFIG_FW_RW_OFF,
		      CONFIG_FW_RW_SIZE - RSANUMBYTES);
	hash = SHA256_final(&ctx);
	good = rsa_verify(&pkey, (void *)rw_sig, (void *)hash, rsa_workbuf);
	if (good) {
		CPRINTS("RW image verified\n");
		/* Jump to the RW firmware */
		system_run_image_copy(SYSTEM_IMAGE_RW);
	} else {
		CPRINTS("RSA verify FAILED\n");
		pd_log_event(PD_EVENT_ACC_RW_FAIL, 0, 0, NULL);
		/* RW firmware is invalid : do not jump there */
		if (system_is_locked())
			system_disable_jump();
	}
	shared_mem_release(rsa_workbuf);
}
