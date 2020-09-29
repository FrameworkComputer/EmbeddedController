/*
 * Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* SHA256 module for Chrome EC */
#include "common.h"
#include "sha256.h"
#include "util.h"

enum ncl_status {
	NCL_STATUS_OK,
	NCL_STATUS_FAIL,
	NCL_STATUS_INVALID_PARAM,
	NCL_STATUS_PARAM_NOT_SUPPORTED,
	NCL_STATUS_SYSTEM_BUSY,
	NCL_STATUS_AUTHENTICATION_FAIL,
	NCL_STATUS_NO_RESPONSE,
	NCL_STATUS_HARDWARE_ERROR,
};

enum ncl_sha_type {
	NCL_SHA_TYPE_2_256 = 0,
	NCL_SHA_TYPE_2_384 = 1,
	NCL_SHA_TYPE_2_512 = 2,
	NCL_SHA_TYPE_NUM
};

/*
 * The base address of the table that holds the function pointer for each
 * SHA256 API in ROM.
 */
#define NCL_SHA_BASE_ADDR          0x00000100UL
struct ncl_sha {
	/* Get the SHA context size required by SHA APIs. */
	uint32_t (*get_context_size)(void);
	/* Initial SHA context. */
	enum ncl_status (*init_context)(void *ctx);
	/* Finalize SHA context. */
	enum ncl_status (*finalize_context)(void *ctx);
	/* Initiate the SHA hardware module and setups needed parameters. */
	enum ncl_status (*init)(void *ctx);
	/*
	 * Prepare the context buffer for a SHA calculation -  by loading the
	 * initial SHA-256/384/512 parameters.
	 */
	enum ncl_status (*start)(void *ctx, enum ncl_sha_type type);
	/*
	 * Updates the SHA calculation with the additional data. When the
	 * function returns, the hardware and memory buffer shall be ready to
	 * accept new data * buffers for SHA calculation and changes to the data
	 * in data buffer should no longer effect the SHA calculation.
	 */
	enum ncl_status (*update)(void *ctx, const uint8_t *data, uint32_t Len);
	/* Return the SHA result (digest.) */
	enum ncl_status (*finish)(void *ctx, uint8_t *hashDigest);
	/* Perform a complete SHA calculation */
	enum ncl_status (*calc)(void *ctx, enum ncl_sha_type type,
			const uint8_t *data, uint32_t Len, uint8_t *hashDigest);
	/* Power on/off the SHA module. */
	enum ncl_status (*power)(void *ctx, uint8_t enable);
	/* Reset the SHA hardware and terminate any in-progress operations. */
	enum ncl_status (*reset)(void *ctx);
};

#define NCL_SHA ((const struct ncl_sha *)NCL_SHA_BASE_ADDR)

void SHA256_init(struct sha256_ctx *ctx)
{
	NCL_SHA->init_context(ctx->handle);
	NCL_SHA->power(ctx->handle, 1);
	NCL_SHA->init(ctx->handle);
	NCL_SHA->reset(ctx->handle);
	NCL_SHA->start(ctx->handle, NCL_SHA_TYPE_2_256);
}

void SHA256_update(struct sha256_ctx *ctx, const uint8_t *data, uint32_t len)
{
	NCL_SHA->update(ctx->handle, data, len);
}

void SHA256_abort(struct sha256_ctx *ctx)
{
	NCL_SHA->reset(ctx->handle);
	NCL_SHA->power(ctx->handle, 0);
	NCL_SHA->finalize_context(ctx->handle);
}

uint8_t *SHA256_final(struct sha256_ctx *ctx)
{
	NCL_SHA->finish(ctx->handle, ctx->buf);
	NCL_SHA->power(ctx->handle, 0);
	NCL_SHA->finalize_context(ctx->handle);
	return ctx->buf;
}

static void hmac_SHA256_step(uint8_t *output, uint8_t mask,
			const uint8_t *key, const int key_len,
			const uint8_t *data, const int data_len)
{
	struct sha256_ctx hmac_ctx;
	uint8_t *key_pad = hmac_ctx.buf;
	uint8_t *tmp;
	int i;

	memset(key_pad, mask, SHA256_BLOCK_SIZE);
	for (i = 0; i < key_len; i++)
		key_pad[i] ^= key[i];

	SHA256_init(&hmac_ctx);
	SHA256_update(&hmac_ctx, key_pad, SHA256_BLOCK_SIZE);
	SHA256_update(&hmac_ctx, data, data_len);
	tmp = SHA256_final(&hmac_ctx);
	memcpy(output, tmp, SHA256_DIGEST_SIZE);
}
/*
 * Note: When the API is called, it will consume about half of TASK_STACK_SIZE
 * because a variable of structure sha256_ctx is declared in the function
 * hmac_SHA256_step.
 */
void hmac_SHA256(uint8_t *output, const uint8_t *key, const int key_len,
		const uint8_t *message, const int message_len)
{
	/* This code does not support key_len > block_size. */
	ASSERT(key_len <= SHA256_BLOCK_SIZE);

	/*
	 * i_key_pad = key (zero-padded) ^ 0x36
	 * output = hash(i_key_pad || message)
	 * (Use output as temporary buffer)
	 */
	hmac_SHA256_step(output, 0x36, key, key_len, message, message_len);

	/*
	 * o_key_pad = key (zero-padded) ^ 0x5c
	 * output = hash(o_key_pad || output)
	 */
	hmac_SHA256_step(output, 0x5c, key, key_len, output,
				SHA256_DIGEST_SIZE);
}
