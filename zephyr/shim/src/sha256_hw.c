/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* SHA256 module for Chrome EC */

#include "sha256.h"

#include <zephyr/crypto/crypto.h>
#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(sha256_hw_shim, CONFIG_CRYPTO_LOG_LEVEL);

static const struct device *sha256_hw_dev =
	DEVICE_DT_GET(DT_CHOSEN(cros_ec_sha));

void SHA256_init(struct sha256_ctx *ctx)
{
	int ret;
	struct hash_ctx *hash_ctx = &ctx->hash_sha256;

	hash_ctx->flags = CAP_SYNC_OPS | CAP_SEPARATE_IO_BUFS;
	ret = hash_begin_session(sha256_hw_dev, hash_ctx,
				 CRYPTO_HASH_ALGO_SHA256);
	if (ret != 0) {
		LOG_ERR("SHA256 Init Fail");
	}
}

void SHA256_update(struct sha256_ctx *ctx, const uint8_t *data, uint32_t len)
{
	int ret;
	struct hash_pkt pkt = {
		.in_buf = (uint8_t *)data,
		.in_len = len,
		.out_buf = ctx->buf,
	};
	struct hash_ctx *hash_ctx = &ctx->hash_sha256;

	ret = hash_update(hash_ctx, &pkt);

	if (ret != 0) {
		LOG_ERR("SHA256 Update Fail");
	}
}

void SHA256_abort(struct sha256_ctx *ctx)
{
	struct hash_ctx *hash_ctx = &ctx->hash_sha256;

	hash_free_session(sha256_hw_dev, hash_ctx);
}

uint8_t *SHA256_final(struct sha256_ctx *ctx)
{
	int ret;
	struct hash_pkt pkt = {
		.in_buf = NULL,
		.in_len = 0,
		.out_buf = ctx->buf,
	};
	struct hash_ctx *hash_ctx = &ctx->hash_sha256;

	/*
	 * Call hash_compute function with zero input data to finish SHA256
	 * computation and get the digest.
	 */
	ret = hash_compute(hash_ctx, &pkt);

	if (ret != 0) {
		LOG_ERR("SHA256 Final Fail");
	}

	hash_free_session(sha256_hw_dev, hash_ctx);
	return ctx->buf;
}

static void hmac_SHA256_step(uint8_t *output, uint8_t mask, const uint8_t *key,
			     const int key_len, const uint8_t *data,
			     const int data_len)
{
	struct sha256_ctx hmac_ctx;
	uint8_t *key_pad = hmac_ctx.buf;
	int i;

	memset(hmac_ctx.buf, mask, sizeof(hmac_ctx.buf));
	for (i = 0; i < key_len; i++)
		key_pad[i] ^= key[i];

	SHA256_init(&hmac_ctx);
	SHA256_update(&hmac_ctx, key_pad, SHA256_BLOCK_SIZE);
	SHA256_update(&hmac_ctx, data, data_len);
	SHA256_final(&hmac_ctx);
	__ASSERT(sizeof(hmac_ctx.buf) <= SHA256_BLOCK_SIZE,
		 "hmac buf size > SHA256 block size");
	memcpy(output, hmac_ctx.buf, SHA256_DIGEST_SIZE);
}

/*
 * Note: When the API is called, it will consume sizeof(struct sha256_ctx) of
 * TASK_STACK_SIZE because a variable of structure sha256_ctx is declared
 * in the function hmac_SHA256_step.
 */
void hmac_SHA256(uint8_t *output, const uint8_t *key, const int key_len,
		 const uint8_t *message, const int message_len)
{
	/* This code does not support key_len > block_size. */
	__ASSERT(key_len <= SHA256_BLOCK_SIZE,
		 "Key length > SHA256 block size");

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

static int zephyr_shim_init_sha256(void)
{
	if (!device_is_ready(sha256_hw_dev)) {
		k_oops();
	}

	return 0;
}
SYS_INIT(zephyr_shim_init_sha256, APPLICATION, 0);
