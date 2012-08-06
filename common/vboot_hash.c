/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Verified boot hash computing module for Chrome EC */

#include "common.h"
#include "console.h"
#include "cryptolib.h"
#include "hooks.h"
#include "host_command.h"
#include "system.h"
#include "task.h"
#include "timer.h"
#include "util.h"
#include "watchdog.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_VBOOT, outstr)
#define CPRINTF(format, args...) cprintf(CC_VBOOT, format, ## args)

struct vboot_hash_tag {
	uint8_t hash[SHA256_DIGEST_SIZE];
	uint32_t offset;
	uint32_t size;
};

#define VBOOT_HASH_SYSJUMP_TAG 0x5648 /* "VH" */
#define VBOOT_HASH_SYSJUMP_VERSION 1
#define CHUNK_SIZE 1024

static uint32_t data_offset;
static uint32_t data_size;
static uint32_t curr_pos;
static const uint8_t *hash;   /* Hash, or NULL if not valid */
static int want_abort;

static SHA256_CTX ctx;


/* Return non-zero if a hash operation is in progress */
static int vboot_hash_in_progress(void)
{
	if (hash)
		return 0;  /* Already done */
	return data_size ? 1 : 0;  /* Nothing to hash */
}


/*
 * Start computing a hash of <size> bytes of data at flash offset <offset>.
 * If nonce_size is non-zero, prefixes the <nonce> onto the data to be
 * hashed.  Returns non-zero if error.
 */
static int vboot_hash_start(uint32_t offset, uint32_t size,
			    const uint8_t *nonce, int nonce_size)
{
	/* Fail if hash computation is already in progress */
	if (vboot_hash_in_progress())
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

	/* Restart the hash computation */
	CPRINTF("[%T hash start 0x%08x 0x%08x]\n", offset, size);
	SHA256_init(&ctx);
	if (nonce_size)
		SHA256_update(&ctx, nonce, nonce_size);

	/* Wake the hash task */
	task_wake(TASK_ID_VBOOTHASH);

	return EC_SUCCESS;
}


/* Abort hash currently in progress, if any. */
static void vboot_hash_abort(void)
{
	if (vboot_hash_in_progress())
		want_abort = 1;
}


static void vboot_hash_init(void)
{
	const struct vboot_hash_tag *tag;
	int version, size;

	tag = (const struct vboot_hash_tag *)system_get_jump_tag(
		VBOOT_HASH_SYSJUMP_TAG, &version, &size);
	if (tag && version == VBOOT_HASH_SYSJUMP_VERSION &&
	    size == sizeof(*tag)) {
		/* Already computed a hash, so don't recompute */
		CPRINTF("[%T hash precomputed]\n");
		hash = tag->hash;
		data_offset = tag->offset;
		data_size = tag->size;
	} else {
		/* Start computing the hash of firmware A */
		vboot_hash_start(CONFIG_FW_RW_OFF,
				 system_get_image_used(SYSTEM_IMAGE_RW),
				 NULL, 0);
	}
}


void vboot_hash_task(void)
{
	vboot_hash_init();

	while (1) {
		if (!vboot_hash_in_progress()) {
			/* Nothing to do, so go back to sleep */
			task_wait_event(-1);
		} else if (want_abort) {
			/* Abort hash computation currently in progress */
			CPRINTF("[%T hash abort]\n");
			data_size = 0;
			want_abort = 0;
		} else {
			/* Compute the next chunk of hash */
			int size = MIN(CHUNK_SIZE, data_size - curr_pos);

			SHA256_update(&ctx,
				      (const uint8_t *)(CONFIG_FLASH_BASE +
							data_offset + curr_pos),
				      size);
			curr_pos += size;
			if (curr_pos >= data_size) {
				hash = SHA256_final(&ctx);
				CPRINTF("[%T hash done %.*h]\n",
					SHA256_DIGEST_SIZE, hash);
			}

			/*
			 * Let other tasks (really, just the watchdog task)
			 * run for a bit.
			 */
			usleep(100);
		}
	}
}

/*****************************************************************************/
/* Hooks */

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

/****************************************************************************/
/* Console commands */

static int command_hash(int argc, char **argv)
{
	uint32_t offset = CONFIG_FW_RW_OFF;
	uint32_t size = CONFIG_FW_RW_SIZE;
	char *e;

	if (argc == 1) {
		ccprintf("Offset: 0x%08x\n", data_offset);
		ccprintf("Size:   0x%08x (%d)\n", data_size, data_size);
		ccprintf("Digest: ");
		if (vboot_hash_in_progress())
			ccprintf("(in progress)\n");
		else
			ccprintf("%.*h\n", SHA256_DIGEST_SIZE, hash);

		return EC_SUCCESS;
	}

	if (argc == 2 && !strcasecmp(argv[1], "abort")) {
		vboot_hash_abort();
		return EC_SUCCESS;
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
			"[abort] | [<offset> <size> [<nonce>]]",
			"Request hash recomputation",
			NULL);

/****************************************************************************/
/* Host commands */

/* Fill in the response with the current hash status */
static void fill_response(struct ec_response_vboot_hash *r)
{
	if (vboot_hash_in_progress())
		r->status = EC_VBOOT_HASH_STATUS_BUSY;
	else if (hash) {
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

static int host_command_vboot_hash(struct host_cmd_handler_args *args)
{
	const struct ec_params_vboot_hash *p = args->params;
	struct ec_response_vboot_hash *r = args->response;
	int rv;

	switch (p->cmd) {
	case EC_VBOOT_HASH_GET:
		fill_response(r);
		args->response_size = sizeof(*r);
		return EC_RES_SUCCESS;

	case EC_VBOOT_HASH_ABORT:
		vboot_hash_abort();
		return EC_RES_SUCCESS;

	case EC_VBOOT_HASH_START:
		if (p->hash_type != EC_VBOOT_HASH_TYPE_SHA256)
			return EC_RES_INVALID_PARAM;
		if (p->nonce_size > sizeof(p->nonce_data))
			return EC_RES_INVALID_PARAM;

		rv = vboot_hash_start(p->offset, p->size, p->nonce_data,
				      p->nonce_size);

		if (rv == EC_SUCCESS)
			return EC_RES_SUCCESS;
		else if (rv == EC_ERROR_INVAL)
			return EC_RES_INVALID_PARAM;
		else
			return EC_RES_ERROR;

	case EC_VBOOT_HASH_RECALC:
		if (p->hash_type != EC_VBOOT_HASH_TYPE_SHA256)
			return EC_RES_INVALID_PARAM;
		if (p->nonce_size > sizeof(p->nonce_data))
			return EC_RES_INVALID_PARAM;

		rv = vboot_hash_start(p->offset, p->size, p->nonce_data,
				      p->nonce_size);
		if (rv == EC_ERROR_INVAL)
			return EC_RES_INVALID_PARAM;
		else if (rv != EC_SUCCESS)
			return EC_RES_ERROR;

		/* Wait for hash to finish */
		while (vboot_hash_in_progress())
			usleep(1000);

		fill_response(r);
		args->response_size = sizeof(*r);
		return EC_RES_SUCCESS;

	default:
		return EC_RES_INVALID_PARAM;
	}
}
DECLARE_HOST_COMMAND(EC_CMD_VBOOT_HASH,
		     host_command_vboot_hash,
		     EC_VER_MASK(0));
