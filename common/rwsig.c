/* Copyright 2014 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * Implementation of the RW firmware signature verification and jump.
 */

#include "console.h"
#include "cros_version.h"
#include "ec_commands.h"
#include "flash.h"
#include "host_command.h"
#include "rollback.h"
#include "rsa.h"
#include "rwsig.h"
#include "sha256.h"
#include "shared_mem.h"
#include "system.h"
#include "task.h"
#include "usb_pd.h"
#include "util.h"
#include "vb21_struct.h"
#include "vboot.h"

/* Console output macros */
#define CPRINTF(format, args...) cprintf(CC_SYSTEM, format, ##args)
#define CPRINTS(format, args...) cprints(CC_SYSTEM, format, ##args)

#if !defined(CONFIG_MAPPED_STORAGE) && !defined(CONFIG_ZTEST)
#error rwsig implementation assumes mem-mapped storage.
#endif

void rwsig_jump_now(void)
{
	/* Protect all flash before jumping to RW. */

	/* This may do nothing if WP is not enabled, RO is not protected. */
	crec_flash_set_protect(EC_FLASH_PROTECT_ALL_NOW, -1);

	/*
	 * For chips that does not support EC_FLASH_PROTECT_ALL_NOW, use
	 * EC_FLASH_PROTECT_ALL_AT_BOOT.
	 */
	if (system_is_locked() &&
	    !(crec_flash_get_protect() & EC_FLASH_PROTECT_ALL_NOW)) {
		crec_flash_set_protect(EC_FLASH_PROTECT_ALL_AT_BOOT, -1);

		if (!(crec_flash_get_protect() & EC_FLASH_PROTECT_ALL_NOW) &&
		    crec_flash_get_protect() & EC_FLASH_PROTECT_ALL_AT_BOOT) {
			/*
			 * If flash protection is still not enabled (some chips
			 * may be able to enable it immediately), reboot.
			 */
			cflush();
			system_reset(SYSTEM_RESET_HARD |
				     SYSTEM_RESET_PRESERVE_FLAGS);
		}
	}

	/* When system is locked, only boot to RW if all flash is protected. */
	if (!system_is_locked() ||
	    crec_flash_get_protect() & EC_FLASH_PROTECT_ALL_NOW)
		system_run_image_copy(EC_IMAGE_RW);
}

/*
 * Check that memory between rwdata[start] and rwdata[len-1] is filled
 * with ones. data, start and len must be aligned on 4-byte boundary.
 */
static int check_padding(const uint8_t *data, unsigned int start,
			 unsigned int len)
{
	unsigned int i;
	const uint32_t *data32 = (const uint32_t *)data;

	if ((start % 4) != 0 || (len % 4) != 0)
		return 0;

	for (i = start / 4; i < len / 4; i++) {
		if (data32[i] != 0xffffffff)
			return 0;
	}

	return 1;
}

int rwsig_check_signature(void)
{
	struct sha256_ctx ctx;
	int res;
	const struct rsa_public_key *key;
	const uint8_t *sig;
	uint8_t *hash;
	uint32_t *rsa_workbuf = NULL;
	int good = 0;

#ifdef CONFIG_MAPPED_STORAGE
	const uint8_t *rwdata = (uint8_t *)CONFIG_MAPPED_STORAGE_BASE +
				CONFIG_EC_WRITABLE_STORAGE_OFF;
	/* RW firmware reset vector */
	uint32_t *const rw_rst = (uint32_t *)(CONFIG_PROGRAM_MEMORY_BASE +
					      CONFIG_RW_MEM_OFF + 4);
#elif defined(CONFIG_ZTEST)
	static uint8_t rwdata[CONFIG_RW_SIZE];
	const uint32_t *const rw_rst = (const uint32_t *)(rwdata + 4);

	crec_flash_read(CONFIG_EC_WRITABLE_STORAGE_OFF, CONFIG_RW_SIZE, rwdata);
#endif

	unsigned int rwlen;
#ifdef CONFIG_RWSIG_TYPE_RWSIG
	const struct vb21_packed_key *vb21_key;
	const struct vb21_signature *vb21_sig;
#endif
#ifdef CONFIG_ROLLBACK
	int32_t rw_rollback_version;
	int32_t min_rollback_version;
#endif

	/* Check if we have a RW firmware flashed */
	if (*rw_rst == 0xffffffff)
		goto out;

	CPRINTS("Verifying RW image...");

#ifdef CONFIG_ROLLBACK
	rw_rollback_version = system_get_rollback_version(EC_IMAGE_RW);
	min_rollback_version = rollback_get_minimum_version();

	if (rw_rollback_version < 0 || min_rollback_version < 0 ||
	    rw_rollback_version < min_rollback_version) {
		CPRINTS("Rollback error (%d < %d)", rw_rollback_version,
			min_rollback_version);
		goto out;
	}
#endif

	/* Large buffer for RSA computation : could be re-use afterwards... */
	res = SHARED_MEM_ACQUIRE_CHECK(3 * RSANUMBYTES, (char **)&rsa_workbuf);
	if (res) {
		CPRINTS("No memory for RW verification");
		goto out;
	}

#ifdef CONFIG_RWSIG_TYPE_USBPD1
	key = (const struct rsa_public_key *)CONFIG_RO_PUBKEY_ADDR;
	sig = (const uint8_t *)CONFIG_RW_SIG_ADDR;
	rwlen = CONFIG_RW_SIZE - CONFIG_RW_SIG_SIZE;
#elif defined(CONFIG_RWSIG_TYPE_RWSIG)
	vb21_key = vb21_get_packed_key();

#ifdef CONFIG_MAPPED_STORAGE
	vb21_sig = (const struct vb21_signature *)CONFIG_RWSIG_READ_ADDR;
#elif defined(CONFIG_ZTEST)
	vb21_sig = (const struct vb21_signature *)(rwdata + RW_SIG_OFFSET);
#endif

	if (vb21_key->c.magic != VB21_MAGIC_PACKED_KEY ||
	    vb21_key->key_size != sizeof(struct rsa_public_key)) {
		CPRINTS("Invalid key.");
		goto out;
	}

	key = (const struct rsa_public_key *)((const uint8_t *)vb21_key +
					      vb21_key->key_offset);

	/*
	 * TODO(crbug.com/690773): We could verify other parameters such
	 * as sig_alg/hash_alg actually matches what we build for.
	 */
	if (vb21_sig->c.magic != VB21_MAGIC_SIGNATURE ||
	    vb21_sig->sig_size != RSANUMBYTES ||
	    vb21_key->sig_alg != vb21_sig->sig_alg ||
	    vb21_key->hash_alg != vb21_sig->hash_alg ||
	    /* Validity check signature offset and data size. */
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
	 *
	 * If the RW region can be protected independently
	 * (CONFIG_FLASH_PROTECT_RW is defined), and system is locked, we only
	 * increment the rollback if RW is currently protected.
	 *
	 * Otherwise, we immediately increment the rollback version.
	 */
	if (rw_rollback_version != min_rollback_version
#ifdef CONFIG_FLASH_PROTECT_RW
	    && ((!system_is_locked() ||
		 crec_flash_get_protect() & EC_FLASH_PROTECT_RW_NOW))
#endif
	) {
		/*
		 * This will fail if the rollback block is protected (RW image
		 * will unprotect that block later on).
		 */
		int ret = rollback_update_version(rw_rollback_version);

		if (ret == 0) {
			CPRINTS("Rollback updated to %d", rw_rollback_version);
		} else if (ret != EC_ERROR_ACCESS_DENIED) {
			CPRINTS("Rollback update error %d", ret);
			good = 0;
		}
	}
#endif
out:
	CPRINTS("RW verify %s", good ? "OK" : "FAILED");

	if (!good) {
		pd_log_event(PD_EVENT_ACC_RW_FAIL, 0, 0, NULL);
		/* RW firmware is invalid : do not jump there */
		if (system_is_locked())
			system_disable_jump();
	}
	if (rsa_workbuf)
		shared_mem_release(rsa_workbuf);

	return good;
}

#ifdef HAS_TASK_RWSIG
#define TASK_EVENT_ABORT TASK_EVENT_CUSTOM_BIT(0)
#define TASK_EVENT_CONTINUE TASK_EVENT_CUSTOM_BIT(1)

static enum rwsig_status rwsig_status;

test_mockable enum rwsig_status rwsig_get_status(void)
{
	return rwsig_status;
}

void rwsig_abort(void)
{
	task_set_event(TASK_ID_RWSIG, TASK_EVENT_ABORT);
}

void rwsig_continue(void)
{
	task_set_event(TASK_ID_RWSIG, TASK_EVENT_CONTINUE);
}

void rwsig_task(void *u)
{
	uint32_t evt;

	if (system_get_image_copy() != EC_IMAGE_RO)
		goto exit;

	/* Stay in RO if we were asked to when reset. */
	if (system_get_reset_flags() & EC_RESET_FLAG_STAY_IN_RO) {
		rwsig_status = RWSIG_ABORTED;
		goto exit;
	}

	rwsig_status = RWSIG_IN_PROGRESS;
	if (!rwsig_check_signature()) {
		rwsig_status = RWSIG_INVALID;
		goto exit;
	}
	rwsig_status = RWSIG_VALID;

	/* Jump to RW after a timeout */
	evt = task_wait_event(CONFIG_RWSIG_JUMP_TIMEOUT);

	/* Jump now if we timed out, or were told to continue. */
	if (evt == TASK_EVENT_TIMER || evt == TASK_EVENT_CONTINUE)
		rwsig_jump_now();
	else
		rwsig_status = RWSIG_ABORTED;

exit:
	/* We're done, yield forever. */
	while (1)
		task_wait_event(-1);
}

static enum ec_status rwsig_cmd_action(struct host_cmd_handler_args *args)
{
	const struct ec_params_rwsig_action *p = args->params;

	switch (p->action) {
	case RWSIG_ACTION_ABORT:
		rwsig_abort();
		break;
	case RWSIG_ACTION_CONTINUE:
		rwsig_continue();
		break;
	default:
		return EC_RES_INVALID_PARAM;
	}
	args->response_size = 0;
	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_RWSIG_ACTION, rwsig_cmd_action, EC_VER_MASK(0));

#else /* !HAS_TASK_RWSIG */
static enum ec_status rwsig_cmd_check_status(struct host_cmd_handler_args *args)
{
	struct ec_response_rwsig_check_status *r = args->response;

	memset(r, 0, sizeof(*r));
	r->status = rwsig_check_signature();
	args->response_size = sizeof(*r);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_RWSIG_CHECK_STATUS, rwsig_cmd_check_status,
		     EC_VER_MASK(0));
#endif
