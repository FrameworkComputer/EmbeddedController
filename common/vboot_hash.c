/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Verified boot hash computing module for Chrome EC */

#include "common.h"
#include "console.h"
#include "flash.h"
#include "hooks.h"
#include "host_command.h"
#include "sha256.h"
#include "shared_mem.h"
#include "system.h"
#include "task.h"
#include "timer.h"
#include "util.h"
#include "watchdog.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_VBOOT, outstr)
#define CPRINTS(format, args...) cprints(CC_VBOOT, format, ## args)

struct vboot_hash_tag {
	uint8_t hash[SHA256_DIGEST_SIZE];
	uint32_t offset;
	uint32_t size;
};

#define VBOOT_HASH_SYSJUMP_TAG 0x5648 /* "VH" */
#define VBOOT_HASH_SYSJUMP_VERSION 1

#define CHUNK_SIZE 1024       /* Bytes to hash per deferred call */
#define WORK_INTERVAL_US 100  /* Delay between deferred calls */

static uint32_t data_offset;
static uint32_t data_size;
static uint32_t curr_pos;
static const uint8_t *hash;   /* Hash, or NULL if not valid */
static int want_abort;
static int in_progress;

static struct sha256_ctx ctx;

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
	}
}

#ifndef CONFIG_MAPPED_STORAGE

static void vboot_hash_next_chunk(void);

static int read_and_hash_chunk(int offset, int size)
{
	char *buf;
	int rv;

	if (size == 0)
		return EC_SUCCESS;

	rv = shared_mem_acquire(size, &buf);
	if (rv == EC_ERROR_BUSY) {
		/* Couldn't update hash right now; try again later */
		hook_call_deferred(vboot_hash_next_chunk, WORK_INTERVAL_US);
		return rv;
	} else if (rv != EC_SUCCESS) {
		vboot_hash_abort();
		return rv;
	}

	rv = flash_read(offset, size, buf);
	if (rv == EC_SUCCESS)
		SHA256_update(&ctx, (const uint8_t *)buf, size);
	else
		vboot_hash_abort();

	shared_mem_release(buf);
	return rv;
}

#endif

/**
 * Do next chunk of hashing work, if any.
 */
static void vboot_hash_next_chunk(void)
{
	int size;

	/* Handle abort */
	if (want_abort) {
		in_progress = 0;
		vboot_hash_abort();
		return;
	}

	/* Compute the next chunk of hash */
	size = MIN(CHUNK_SIZE, data_size - curr_pos);

#ifdef CONFIG_MAPPED_STORAGE
	SHA256_update(&ctx, (const uint8_t *)(CONFIG_MAPPED_STORAGE_BASE +
					      data_offset + curr_pos), size);
#else
	if (read_and_hash_chunk(data_offset + curr_pos, size) != EC_SUCCESS)
		return;
#endif

	curr_pos += size;
	if (curr_pos >= data_size) {
		/* Store the final hash */
		hash = SHA256_final(&ctx);
		CPRINTS("hash done %.*h", SHA256_DIGEST_SIZE, hash);

		in_progress = 0;

		/* Handle receiving abort during finalize */
		if (want_abort)
			vboot_hash_abort();

		return;
	}

	/* If we're still here, more work to do; come back later */
	hook_call_deferred(vboot_hash_next_chunk, WORK_INTERVAL_US);
}
DECLARE_DEFERRED(vboot_hash_next_chunk);

/**
 * Start computing a hash of <size> bytes of data at flash offset <offset>.
 *
 * If nonce_size is non-zero, prefixes the <nonce> onto the data to be hashed.
 * Returns non-zero if error.
 */
static int vboot_hash_start(uint32_t offset, uint32_t size,
			    const uint8_t *nonce, int nonce_size)
{
	/* Fail if hash computation is already in progress */
	if (in_progress)
		return EC_ERROR_BUSY;

	/*
	 * Make sure request fits inside flash.  That is, you can't use this
	 * command to peek at other memory.
	 */
	if (offset > CONFIG_FLASH_SIZE || size > CONFIG_FLASH_SIZE ||
	    offset + size > CONFIG_FLASH_SIZE || nonce_size < 0) {
		return EC_ERROR_INVAL;
	}

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

	hook_call_deferred(vboot_hash_next_chunk, 0);

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

static void vboot_hash_init(void)
{
#ifdef CONFIG_SAVE_VBOOT_HASH
	const struct vboot_hash_tag *tag;
	int version, size;

	tag = (const struct vboot_hash_tag *)system_get_jump_tag(
		VBOOT_HASH_SYSJUMP_TAG, &version, &size);
	if (tag && version == VBOOT_HASH_SYSJUMP_VERSION &&
	    size == sizeof(*tag)) {
		/* Already computed a hash, so don't recompute */
		CPRINTS("hash precomputed");
		hash = tag->hash;
		data_offset = tag->offset;
		data_size = tag->size;
	} else
#endif
	{
		/* Start computing the hash of RW firmware */
		vboot_hash_start(CONFIG_EC_WRITABLE_STORAGE_OFF +
				 CONFIG_RW_STORAGE_OFF,
				 system_get_image_used(SYSTEM_IMAGE_RW),
				 NULL, 0);
	}
}
DECLARE_HOOK(HOOK_INIT, vboot_hash_init, HOOK_PRIO_INIT_VBOOT_HASH);

#ifdef CONFIG_SAVE_VBOOT_HASH

static int vboot_hash_preserve_state(void)
{
	struct vboot_hash_tag tag;

	/* If we haven't finished our hash, nothing to save */
	if (!hash)
		return EC_SUCCESS;

	memcpy(tag.hash, hash, sizeof(tag.hash));
	tag.offset = data_offset;
	tag.size = data_size;
	system_add_jump_tag(VBOOT_HASH_SYSJUMP_TAG,
			    VBOOT_HASH_SYSJUMP_VERSION,
			    sizeof(tag), &tag);
	return EC_SUCCESS;
}
DECLARE_HOOK(HOOK_SYSJUMP, vboot_hash_preserve_state, HOOK_PRIO_DEFAULT);

#endif

/****************************************************************************/
/* Console commands */
#ifdef CONFIG_CMD_HASH
static int command_hash(int argc, char **argv)
{
	uint32_t offset = CONFIG_EC_WRITABLE_STORAGE_OFF +
			  CONFIG_RW_STORAGE_OFF;
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
		else if (hash)
			ccprintf("%.*h\n", SHA256_DIGEST_SIZE, hash);
		else
			ccprintf("(invalid)\n");

		return EC_SUCCESS;
	}

	if (argc == 2) {
		if (!strcasecmp(argv[1], "abort")) {
			vboot_hash_abort();
			return EC_SUCCESS;
		} else if (!strcasecmp(argv[1], "rw")) {
			return vboot_hash_start(
				CONFIG_EC_WRITABLE_STORAGE_OFF +
				CONFIG_RW_STORAGE_OFF,
				system_get_image_used(SYSTEM_IMAGE_RW),
				NULL, 0);
		} else if (!strcasecmp(argv[1], "ro")) {
			return vboot_hash_start(
				CONFIG_EC_PROTECTED_STORAGE_OFF +
				CONFIG_RO_STORAGE_OFF,
				system_get_image_used(SYSTEM_IMAGE_RO),
				NULL, 0);
		}
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

		return vboot_hash_start(offset, size,
					(const uint8_t *)&nonce,
					sizeof(nonce));
	} else
		return vboot_hash_start(offset, size, NULL, 0);
}
DECLARE_CONSOLE_COMMAND(hash, command_hash,
			"[abort | ro | rw] | [<offset> <size> [<nonce>]]",
			"Request hash recomputation",
			NULL);
#endif /* CONFIG_CMD_HASH */
/****************************************************************************/
/* Host commands */

/**
 * Return the offset of the RO or RW region if the either region is specifically
 * requested otherwise return the current hash offset.
 */
static int get_offset(int offset)
{
	if (offset == EC_VBOOT_HASH_OFFSET_RO)
		return CONFIG_EC_PROTECTED_STORAGE_OFF + CONFIG_RO_STORAGE_OFF;
	if (offset == EC_VBOOT_HASH_OFFSET_RW)
		return CONFIG_EC_WRITABLE_STORAGE_OFF + CONFIG_RW_STORAGE_OFF;
	return data_offset;
}

/* Fill in the response with the current hash status */
static void fill_response(struct ec_response_vboot_hash *r,
			  int request_offset)
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
		ASSERT(SHA256_DIGEST_SIZE < sizeof(r->hash_digest));
		memcpy(r->hash_digest, hash, SHA256_DIGEST_SIZE);
	} else
		r->status = EC_VBOOT_HASH_STATUS_NONE;
}

/**
 * Start computing a hash, with sanity checking on params.
 *
 * @return EC_RES_SUCCESS if success, or other result code on error.
 */
static int host_start_hash(const struct ec_params_vboot_hash *p)
{
	int offset = p->offset;
	int size = p->size;
	int rv;

	/* Sanity-check input params */
	if (p->hash_type != EC_VBOOT_HASH_TYPE_SHA256)
		return EC_RES_INVALID_PARAM;
	if (p->nonce_size > sizeof(p->nonce_data))
		return EC_RES_INVALID_PARAM;

	/* Handle special offset values */
	if (offset == EC_VBOOT_HASH_OFFSET_RO) {
		offset = CONFIG_EC_PROTECTED_STORAGE_OFF +
			 CONFIG_RO_STORAGE_OFF;
		size = system_get_image_used(SYSTEM_IMAGE_RO);
	} else if (p->offset == EC_VBOOT_HASH_OFFSET_RW) {
		offset = CONFIG_EC_WRITABLE_STORAGE_OFF + CONFIG_RW_STORAGE_OFF;
		size = system_get_image_used(SYSTEM_IMAGE_RW);
	}

	rv = vboot_hash_start(offset, size, p->nonce_data, p->nonce_size);

	if (rv == EC_SUCCESS)
		return EC_RES_SUCCESS;
	else if (rv == EC_ERROR_INVAL)
		return EC_RES_INVALID_PARAM;
	else
		return EC_RES_ERROR;
}

static int host_command_vboot_hash(struct host_cmd_handler_args *args)
{
	const struct ec_params_vboot_hash *p = args->params;
	struct ec_response_vboot_hash *r = args->response;
	int rv;

	switch (p->cmd) {
	case EC_VBOOT_HASH_GET:
		fill_response(r, p->offset);
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
DECLARE_HOST_COMMAND(EC_CMD_VBOOT_HASH,
		     host_command_vboot_hash,
		     EC_VER_MASK(0));
