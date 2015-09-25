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

/* RW firmware reset vector */
static uint32_t * const rw_rst =
	(uint32_t *)(CONFIG_PROGRAM_MEMORY_BASE + CONFIG_RW_MEM_OFF + 4);

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

	CPRINTS("Verifying RW image...");

	/* Large buffer for RSA computation : could be re-use afterwards... */
	res = shared_mem_acquire(3 * RSANUMBYTES, (char **)&rsa_workbuf);
	if (res) {
		CPRINTS("No memory for RW verification");
		return;
	}

	/* SHA-256 Hash of the RW firmware */
	/* TODO(crosbug.com/p/44803): Do we have to hash the whole region? */
	SHA256_init(&ctx);
	SHA256_update(&ctx, (void *)CONFIG_PROGRAM_MEMORY_BASE
		      + CONFIG_RW_MEM_OFF,
		      CONFIG_RW_SIZE - CONFIG_RW_SIG_SIZE);
	hash = SHA256_final(&ctx);

	good = rsa_verify((const struct rsa_public_key *)CONFIG_RO_PUBKEY_ADDR,
			  (const uint8_t *)CONFIG_RW_SIG_ADDR,
			  hash, rsa_workbuf);
	if (good) {
		CPRINTS("RW image verified");
		/* Jump to the RW firmware */
		system_run_image_copy(SYSTEM_IMAGE_RW);
	} else {
		CPRINTS("RSA verify FAILED");
		pd_log_event(PD_EVENT_ACC_RW_FAIL, 0, 0, NULL);
		/* RW firmware is invalid : do not jump there */
		if (system_is_locked())
			system_disable_jump();
	}
	shared_mem_release(rsa_workbuf);
}
