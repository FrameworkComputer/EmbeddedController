/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * Implementation of the RW firmware signature verification and jump.
 */

#include "console.h"
#include "ec_commands.h"
#include "rollback.h"
#include "rsa.h"
#include "sha256.h"
#include "shared_mem.h"
#include "system.h"
#include "usb_pd.h"
#include "util.h"
#include "vb21_struct.h"
#include "version.h"

/* Console output macros */
#define CPRINTF(format, args...) cprintf(CC_SYSTEM, format, ## args)
#define CPRINTS(format, args...) cprints(CC_SYSTEM, format, ## args)

/* RW firmware reset vector */
static uint32_t * const rw_rst =
	(uint32_t *)(CONFIG_PROGRAM_MEMORY_BASE + CONFIG_RW_MEM_OFF + 4);

/*
 * Check that memory between rwdata[start] and rwdata[len-1] is filled
 * with ones. data, start and len must be aligned on 4-byte boundary.
 */
static int check_padding(const uint8_t *data,
			 unsigned int start, unsigned int len)
{
	unsigned int i;
	const uint32_t *data32 = (const uint32_t *)data;

	if ((start % 4) != 0 || (len % 4) != 0)
		return 0;

	for (i = start/4; i < len/4; i++) {
		if (data32[i] != 0xffffffff)
			return 0;
	}

	return 1;
}

void check_rw_signature(void)
{
	struct sha256_ctx ctx;
	int res;
	const struct rsa_public_key *key;
	const uint8_t *sig;
	uint8_t *hash;
	uint32_t *rsa_workbuf;
	const uint8_t *rwdata = (uint8_t *)CONFIG_PROGRAM_MEMORY_BASE
					+ CONFIG_RW_MEM_OFF;
	int good = 0;

	unsigned int rwlen;
#ifdef CONFIG_RWSIG_TYPE_RWSIG
	const struct vb21_packed_key *vb21_key;
	const struct vb21_signature *vb21_sig;
#endif
#ifdef CONFIG_ROLLBACK
	int32_t rw_rollback_version;
	int32_t min_rollback_version;
#endif

	/* Only the Read-Only firmware needs to do the signature check */
	if (system_get_image_copy() != SYSTEM_IMAGE_RO)
		return;

	/* Check if we have a RW firmware flashed */
	if (*rw_rst == 0xffffffff)
		return;

	CPRINTS("Verifying RW image...");

#ifdef CONFIG_ROLLBACK
	rw_rollback_version = system_get_rollback_version(SYSTEM_IMAGE_RW);
	min_rollback_version = rollback_get_minimum_version();

	if (rw_rollback_version < 0 || min_rollback_version < 0 ||
	    rw_rollback_version < min_rollback_version) {
		CPRINTS("Rollback error (%d < %d)",
			rw_rollback_version, min_rollback_version);
		return;
	}
#endif

	/* Large buffer for RSA computation : could be re-use afterwards... */
	res = shared_mem_acquire(3 * RSANUMBYTES, (char **)&rsa_workbuf);
	if (res) {
		CPRINTS("No memory for RW verification");
		return;
	}

#ifdef CONFIG_RWSIG_TYPE_USBPD1
	key = (const struct rsa_public_key *)CONFIG_RO_PUBKEY_ADDR;
	sig = (const uint8_t *)CONFIG_RW_SIG_ADDR;
	rwlen = CONFIG_RW_SIZE - CONFIG_RW_SIG_SIZE;
#elif defined(CONFIG_RWSIG_TYPE_RWSIG)
	vb21_key = (const struct vb21_packed_key *)CONFIG_RO_PUBKEY_ADDR;
	vb21_sig = (const struct vb21_signature *)CONFIG_RW_SIG_ADDR;

	if (vb21_key->c.magic != VB21_MAGIC_PACKED_KEY ||
	    vb21_key->key_size != sizeof(struct rsa_public_key)) {
		CPRINTS("Invalid key.");
		goto out;
	}

	key = (const struct rsa_public_key *)
		((const uint8_t *)vb21_key + vb21_key->key_offset);

	/*
	 * TODO(crbug.com/690773): We could verify other parameters such
	 * as sig_alg/hash_alg actually matches what we build for.
	 */
	if (vb21_sig->c.magic != VB21_MAGIC_SIGNATURE ||
	    vb21_sig->sig_size != RSANUMBYTES ||
	    vb21_key->sig_alg != vb21_sig->sig_alg ||
	    vb21_key->hash_alg != vb21_sig->hash_alg ||
	    /* Sanity check signature offset and data size. */
	    vb21_sig->sig_offset < sizeof(vb21_sig) ||
	    (vb21_sig->sig_offset + RSANUMBYTES) > CONFIG_RW_SIG_SIZE ||
	    vb21_sig->data_size > (CONFIG_RW_SIZE - CONFIG_RW_SIG_SIZE)) {
		CPRINTS("Invalid signature.");
		goto out;
	}

	sig = (const uint8_t *)vb21_sig + vb21_sig->sig_offset;
	rwlen = vb21_sig->data_size;
#endif

	/*
	 * Check that unverified RW region is actually filled with ones.
	 */
	good = check_padding(rwdata, rwlen,
			CONFIG_RW_SIZE - CONFIG_RW_SIG_SIZE);
	if (!good) {
		CPRINTS("Invalid padding.");
		goto out;
	}

	/* SHA-256 Hash of the RW firmware */
	SHA256_init(&ctx);
	SHA256_update(&ctx, rwdata, rwlen);
	hash = SHA256_final(&ctx);

	good = rsa_verify(key, sig, hash, rsa_workbuf);
	if (!good)
		goto out;

#ifdef CONFIG_ROLLBACK
	/*
	 * Signature verified: we know that rw_rollback_version is valid, check
	 * if rollback information should be updated.
	 */
	if (rw_rollback_version != min_rollback_version) {
		/*
		 * This will fail if the rollback block is protected (RW image
		 * will unprotect that block later on).
		 */
		int ret = rollback_update(rw_rollback_version);

		if (ret == 0) {
			CPRINTS("Rollback updated to %d",
				rw_rollback_version);
		} else if (ret != EC_ERROR_ACCESS_DENIED) {
			CPRINTS("Rollback update error %d", ret);
			good = 0;
		}
	}

	/*
	 * Lock the ROLLBACK region, this will cause the board to reboot if the
	 * region is not already protected.
	 */
	rollback_lock();
#endif
out:
	CPRINTS("RW verify %s", good ? "OK" : "FAILED");

	if (good) {
		/* Jump to the RW firmware */
		system_run_image_copy(SYSTEM_IMAGE_RW);
	} else {
		pd_log_event(PD_EVENT_ACC_RW_FAIL, 0, 0, NULL);
		/* RW firmware is invalid : do not jump there */
		if (system_is_locked())
			system_disable_jump();
	}
	shared_mem_release(rsa_workbuf);
}
