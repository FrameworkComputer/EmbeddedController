/* Copyright 2012 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Verified boot hash computing module for Chrome EC */

/*
 * TODO(b/272518464): Work around coreboot GCC preprocessor bug.
 * #line marks the *next* line, so it is off by one.
 */
#line 13

#include "builtin/assert.h"
#include "clock.h"
#include "common.h"
#include "console.h"
#include "flash.h"
#include "hooks.h"
#include "host_command.h"
#include "printf.h"
#include "sha256.h"
#include "shared_mem.h"
#include "stdbool.h"
#include "stdint.h"
#include "system.h"
#include "task.h"
#include "timer.h"
#include "util.h"
#include "watchdog.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_VBOOT, outstr)
#define CPRINTS(format, args...) cprints(CC_VBOOT, format, ##args)

struct vboot_hash_tag {
	uint8_t hash[SHA256_DIGEST_SIZE];
	uint32_t offset;
	uint32_t size;
};

#define CHUNK_SIZE 1024 /* Bytes to hash per deferred call */
#define WORK_INTERVAL_US 100 /* Delay between deferred calls */

/* Check that CHUNK_SIZE fits in shared memory. */
SHARED_MEM_CHECK_SIZE(CHUNK_SIZE);

static uint32_t data_offset;
static uint32_t data_size;
static uint32_t curr_pos;
static const uint8_t *hash; /* Hash, or NULL if not valid */
static int want_abort;
static int in_progress;
#define VBOOT_HASH_DEFERRED true
#define VBOOT_HASH_BLOCKING false

static
#if (defined(CONFIG_SOC_IT8XXX2_SHA256_HW_ACCELERATE) && \
     !defined(CONFIG_PLATFORM_EC_SHA256_HW_ZEPHYR))
	__attribute__((section(".__sha256_ram_block")))
#endif
	struct sha256_ctx ctx;

int vboot_hash_in_progress(void)
{
	return in_progress;
}

/**
 * Abort hash currently in progress, and invalidate any completed hash.
 */
void vboot_hash_abort(void)
{
	if (in_progress) {
		want_abort = 1;
	} else {
		CPRINTS("hash abort");
		want_abort = 0;
		data_size = 0;
		hash = NULL;
#ifdef CONFIG_SHA256_HW_ACCELERATE
		SHA256_abort(&ctx);
#endif
	}
}

static void vboot_hash_next_chunk(void);
DECLARE_DEFERRED(vboot_hash_next_chunk);

#ifndef CONFIG_MAPPED_STORAGE

static int read_and_hash_chunk(int offset, int size)
{
	char *buf;
	int rv;

	if (size == 0)
		return EC_SUCCESS;

	rv = shared_mem_acquire(size, &buf);
	if (rv == EC_ERROR_BUSY) {
		/* Couldn't update hash right now; try again later */
		hook_call_deferred(&vboot_hash_next_chunk_data,
				   WORK_INTERVAL_US);
		return rv;
	} else if (rv != EC_SUCCESS) {
		vboot_hash_abort();
		return rv;
	}

	rv = crec_flash_read(offset, size, buf);
	if (rv == EC_SUCCESS)
		SHA256_update(&ctx, (const uint8_t *)buf, size);
	else
		vboot_hash_abort();

	shared_mem_release(buf);
	return rv;
}

#endif

#ifdef CONFIG_CONSOLE_VERBOSE
#define SHA256_PRINT_SIZE SHA256_DIGEST_SIZE
#else
#define SHA256_PRINT_SIZE 4
#endif

static void hash_next_chunk(size_t size)
{
#ifdef CONFIG_MAPPED_STORAGE
	crec_flash_lock_mapped_storage(1);
	SHA256_update(&ctx,
		      (const uint8_t *)((uintptr_t)CONFIG_MAPPED_STORAGE_BASE +
					data_offset + curr_pos),
		      size);
	crec_flash_lock_mapped_storage(0);
#else
	if (read_and_hash_chunk(data_offset + curr_pos, size) != EC_SUCCESS)
		return;
#endif
}

static void vboot_hash_all_chunks(void)
{
	char str_buf[hex_str_buf_size(SHA256_PRINT_SIZE)];

	do {
		size_t size = MIN(CHUNK_SIZE, data_size - curr_pos);
		hash_next_chunk(size);
		curr_pos += size;
	} while (curr_pos < data_size);

	hash = SHA256_final(&ctx);
	snprintf_hex_buffer(str_buf, sizeof(str_buf),
			    HEX_BUF(hash, SHA256_PRINT_SIZE));
	CPRINTS("hash done %s", str_buf);
	in_progress = 0;
	clock_enable_module(MODULE_FAST_CPU, 0);

	return;
}

/**
 * Do next chunk of hashing work, if any.
 */
static void vboot_hash_next_chunk(void)
{
	int size;

	/* Handle abort */
	if (want_abort) {
		in_progress = 0;
		clock_enable_module(MODULE_FAST_CPU, 0);
		vboot_hash_abort();
		return;
	}

	/* Compute the next chunk of hash */
	size = MIN(CHUNK_SIZE, data_size - curr_pos);
	hash_next_chunk(size);

	curr_pos += size;
	if (curr_pos >= data_size) {
		char str_buf[hex_str_buf_size(SHA256_PRINT_SIZE)];

		/* Store the final hash */
		hash = SHA256_final(&ctx);

		snprintf_hex_buffer(str_buf, sizeof(str_buf),
				    HEX_BUF(hash, SHA256_PRINT_SIZE));
		CPRINTS("hash done %s", str_buf);

		in_progress = 0;

		clock_enable_module(MODULE_FAST_CPU, 0);

		/* Handle receiving abort during finalize */
		if (want_abort)
			vboot_hash_abort();

		return;
	}

	/* If we're still here, more work to do; come back later */
	hook_call_deferred(&vboot_hash_next_chunk_data, WORK_INTERVAL_US);
}

/**
 *
 * If nonce_size is non-zero, prefixes the <nonce> onto the data to be hashed.
 * Returns non-zero if error.
 */
/**
 * Start computing a hash of <size> bytes of data at flash offset <offset>.
 *
 * @param offset	start address of data on flash to compute hash for.
 * @param size		size of data to compute hash for.
 * @param nonce		nonce to differentiate hash.
 * @param nonce_size	size of nonce.
 * @param deferred	True to hash progressively through deferred calls.
 * 			False to hash with a blocking single call.
 * @return		ec_error_list.
 */
static int vboot_hash_start(uint32_t offset, uint32_t size,
			    const uint8_t *nonce, int nonce_size, bool deferred)
{
	/* Fail if hash computation is already in progress */
	if (in_progress)
		return EC_ERROR_BUSY;

	/*
	 * Make sure request fits inside flash.  That is, you can't use this
	 * command to peek at other memory.
	 */
	if (offset > CONFIG_FLASH_SIZE_BYTES ||
	    size > CONFIG_FLASH_SIZE_BYTES ||
	    offset + size > CONFIG_FLASH_SIZE_BYTES || nonce_size < 0) {
		return EC_ERROR_INVAL;
	}

	clock_enable_module(MODULE_FAST_CPU, 1);
	/* Save new hash request */
	data_offset = offset;
	data_size = size;
	curr_pos = 0;
	hash = NULL;
	want_abort = 0;
	in_progress = 1;

	/* Restart the hash computation */
	CPRINTS("hash start 0x%08x 0x%08x", offset, size);
	SHA256_init(&ctx);
	if (nonce_size)
		SHA256_update(&ctx, nonce, nonce_size);

	if (deferred)
		hook_call_deferred(&vboot_hash_next_chunk_data, 0);
	else
		vboot_hash_all_chunks();

	return EC_SUCCESS;
}

int vboot_hash_invalidate(int offset, int size)
{
	/* Don't invalidate if passed an invalid region */
	if (offset < 0 || size <= 0 || offset + size < 0)
		return 0;

	/* Don't invalidate if hash is already invalid */
	if (!hash)
		return 0;

	/*
	 * Always invalidate zero-size hash. No overlap if passed region is off
	 * either end of hashed region.
	 */
	if (data_size > 0 &&
	    (offset + size <= data_offset || offset >= data_offset + data_size))
		return 0;

	/* Invalidate the hash */
	CPRINTS("hash invalidated 0x%08x 0x%08x", offset, size);
	vboot_hash_abort();
	return 1;
}

/*****************************************************************************/
/* Hooks */

/**
 * Returns the size of a RW copy to be hashed as expected by Softsync.
 */
static uint32_t get_rw_size(void)
{
#ifdef CONFIG_VBOOT_EFS /* Only needed for EFS, which signs and verifies \
			 * entire RW, thus not needed for EFS2, which    \
			 * verifies only the used image size. */
	return CONFIG_RW_SIZE;
#else
	return system_get_image_used(EC_IMAGE_RW);
#endif
}

static void vboot_hash_init(void)
{
#ifdef CONFIG_HOSTCMD_EVENTS
	/*
	 * Don't auto-start hash computation if we've asked the host to enter
	 * recovery mode since we probably won't need the hash. Although
	 * the host is capable of clearing this host event, the host is
	 * likely not even up and running yet in the case of cold boot, due to
	 * the power sequencing task not having run yet.
	 */
	if (!(host_get_events() &
	      EC_HOST_EVENT_MASK(EC_HOST_EVENT_KEYBOARD_RECOVERY)))
#endif
	{
		/*
		 * At this point, it's likely that EFS2 vboot_main() already
		 * requested the RW hash calculation once.
		 *
		 * Start computing the hash of RW firmware only if we haven't
		 * done it before.
		 */
		if (!hash) {
			vboot_hash_start(
				flash_get_rw_offset(system_get_active_copy()),
				get_rw_size(), NULL, 0, VBOOT_HASH_DEFERRED);
		}
	}
}
DECLARE_HOOK(HOOK_INIT, vboot_hash_init, HOOK_PRIO_INIT_VBOOT_HASH);

int vboot_get_rw_hash(const uint8_t **dst)
{
	int rv = vboot_hash_start(flash_get_rw_offset(system_get_active_copy()),
				  get_rw_size(), NULL, 0, VBOOT_HASH_BLOCKING);
	*dst = hash;
	return rv;
}

int vboot_get_ro_hash(const uint8_t **dst)
{
	int rv = vboot_hash_start(CONFIG_EC_PROTECTED_STORAGE_OFF +
					  CONFIG_RO_STORAGE_OFF,
				  system_get_image_used(EC_IMAGE_RO), NULL, 0,
				  VBOOT_HASH_BLOCKING);
	*dst = hash;
	return rv;
}

/**
 * Returns the offset of RO or RW image if the either region is specifically
 * requested otherwise return the current hash offset.
 */
static int get_offset(int offset)
{
	if (offset == EC_VBOOT_HASH_OFFSET_RO)
		return CONFIG_EC_PROTECTED_STORAGE_OFF + CONFIG_RO_STORAGE_OFF;
	if (offset == EC_VBOOT_HASH_OFFSET_ACTIVE)
		return flash_get_rw_offset(system_get_active_copy());
	if (offset == EC_VBOOT_HASH_OFFSET_UPDATE)
		return flash_get_rw_offset(system_get_update_copy());
	return offset;
}

/****************************************************************************/
/* Console commands */
#ifdef CONFIG_CMD_HASH
static int command_hash(int argc, const char **argv)
{
	uint32_t offset =
		CONFIG_EC_WRITABLE_STORAGE_OFF + CONFIG_RW_STORAGE_OFF;
	uint32_t size = CONFIG_RW_SIZE;
	char *e;

	if (argc == 1) {
		ccprintf("Offset: 0x%08x\n", data_offset);
		ccprintf("Size:   0x%08x (%d)\n", data_size, data_size);
		ccprintf("Digest: ");
		if (want_abort)
			ccprintf("(aborting)\n");
		else if (in_progress)
			ccprintf("(in progress)\n");
		else if (hash) {
			char str_buf[hex_str_buf_size(SHA256_DIGEST_SIZE)];

			snprintf_hex_buffer(str_buf, sizeof(str_buf),
					    HEX_BUF(hash, SHA256_DIGEST_SIZE));
			ccprintf("%s\n", str_buf);
		} else
			ccprintf("(invalid)\n");

		return EC_SUCCESS;
	}

	if (argc == 2) {
		if (!strcasecmp(argv[1], "abort")) {
			vboot_hash_abort();
			return EC_SUCCESS;
		} else if (!strcasecmp(argv[1], "rw")) {
			return vboot_hash_start(
				get_offset(EC_VBOOT_HASH_OFFSET_ACTIVE),
				get_rw_size(), NULL, 0, VBOOT_HASH_DEFERRED);
		} else if (!strcasecmp(argv[1], "ro")) {
			return vboot_hash_start(
				CONFIG_EC_PROTECTED_STORAGE_OFF +
					CONFIG_RO_STORAGE_OFF,
				system_get_image_used(EC_IMAGE_RO), NULL, 0,
				VBOOT_HASH_DEFERRED);
		}
		return EC_ERROR_PARAM2;
	}

	if (argc >= 3) {
		offset = strtoi(argv[1], &e, 0);
		if (*e)
			return EC_ERROR_PARAM1;

		size = strtoi(argv[2], &e, 0);
		if (*e)
			return EC_ERROR_PARAM2;
	}

	if (argc == 4) {
		int nonce = strtoi(argv[3], &e, 0);
		if (*e)
			return EC_ERROR_PARAM3;

		return vboot_hash_start(offset, size, (const uint8_t *)&nonce,
					sizeof(nonce), VBOOT_HASH_DEFERRED);
	} else
		return vboot_hash_start(offset, size, NULL, 0,
					VBOOT_HASH_DEFERRED);
}
DECLARE_CONSOLE_COMMAND(hash, command_hash,
			"[abort | ro | rw] | [<offset> <size> [<nonce>]]",
			"Request hash recomputation");
#endif /* CONFIG_CMD_HASH */
/****************************************************************************/
/* Host commands */

/* Fill in the response with the current hash status */
static void fill_response(struct ec_response_vboot_hash *r, int request_offset)
{
	if (in_progress)
		r->status = EC_VBOOT_HASH_STATUS_BUSY;
	else if (get_offset(request_offset) == data_offset && hash &&
		 !want_abort) {
		r->status = EC_VBOOT_HASH_STATUS_DONE;
		r->hash_type = EC_VBOOT_HASH_TYPE_SHA256;
		r->digest_size = SHA256_DIGEST_SIZE;
		r->reserved0 = 0;
		r->offset = data_offset;
		r->size = data_size;
		BUILD_ASSERT(sizeof(r->hash_digest) >= SHA256_DIGEST_SIZE);
		memcpy(r->hash_digest, hash, SHA256_DIGEST_SIZE);
	} else
		r->status = EC_VBOOT_HASH_STATUS_NONE;
}

/**
 * Start computing a hash, with validity checking on params.
 *
 * @return EC_RES_SUCCESS if success, or other result code on error.
 */
static int host_start_hash(const struct ec_params_vboot_hash *p)
{
	int offset = p->offset;
	int size = p->size;
	int rv;

	/* Validity-check input params */
	if (p->hash_type != EC_VBOOT_HASH_TYPE_SHA256)
		return EC_RES_INVALID_PARAM;
	if (p->nonce_size > sizeof(p->nonce_data))
		return EC_RES_INVALID_PARAM;

	/* Handle special offset values */
	if (offset == EC_VBOOT_HASH_OFFSET_RO)
		size = system_get_image_used(EC_IMAGE_RO);
	else if ((offset == EC_VBOOT_HASH_OFFSET_ACTIVE) ||
		 (offset == EC_VBOOT_HASH_OFFSET_UPDATE))
		size = get_rw_size();
	offset = get_offset(offset);
	rv = vboot_hash_start(offset, size, p->nonce_data, p->nonce_size,
			      VBOOT_HASH_DEFERRED);

	if (rv == EC_SUCCESS)
		return EC_RES_SUCCESS;
	else if (rv == EC_ERROR_INVAL)
		return EC_RES_INVALID_PARAM;
	else
		return EC_RES_ERROR;
}

static enum ec_status
host_command_vboot_hash(struct host_cmd_handler_args *args)
{
	const struct ec_params_vboot_hash *p = args->params;
	struct ec_response_vboot_hash *r = args->response;
	int rv;

	switch (p->cmd) {
	case EC_VBOOT_HASH_GET:
		if (p->offset || p->size)
			fill_response(r, p->offset);
		else
			fill_response(r, data_offset);

		args->response_size = sizeof(*r);
		return EC_RES_SUCCESS;

	case EC_VBOOT_HASH_ABORT:
		vboot_hash_abort();
		return EC_RES_SUCCESS;

	case EC_VBOOT_HASH_START:
	case EC_VBOOT_HASH_RECALC:
		rv = host_start_hash(p);
		if (rv != EC_RES_SUCCESS)
			return rv;

		/* Wait for hash to finish if command is RECALC */
		if (p->cmd == EC_VBOOT_HASH_RECALC)
			while (in_progress)
				usleep(1000);

		fill_response(r, p->offset);
		args->response_size = sizeof(*r);
		return EC_RES_SUCCESS;

	default:
		return EC_RES_INVALID_PARAM;
	}
}
DECLARE_HOST_COMMAND(EC_CMD_VBOOT_HASH, host_command_vboot_hash,
		     EC_VER_MASK(0));
