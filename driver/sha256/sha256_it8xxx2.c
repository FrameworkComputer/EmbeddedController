/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "builtin/assert.h"
#include "builtin/endian.h"
#include "common.h"
#include "console.h"
#include "sha256.h"
#include "util.h"

#define IT8XXX2_GCTRL_SHA1HASHCTRLR REG8(0x00f0202d)
#define IT8XXX2_GCTRL_SHA1HBADDR REG8(0x00f0202e)
#define IT8XXX2_GCTRL_SHA2HBADDR REG8(0x00f0202f)

static const uint32_t sha256_h0[8] = { 0x6a09e667, 0xbb67ae85, 0x3c6ef372,
				       0xa54ff53a, 0x510e527f, 0x9b05688c,
				       0x1f83d9ab, 0x5be0cd19 };

static const uint32_t sha256_k[64] = {
	0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1,
	0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
	0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
	0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
	0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
	0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
	0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
	0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
	0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
	0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
	0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

void SHA256_init(struct sha256_ctx *ctx)
{
	int i;

	ctx->total_len = 0;
	ctx->w_index = 0;

	for (i = 0; i < ARRAY_SIZE(sha256_h0); i++)
		ctx->h[i] = sha256_h0[i];
	for (i = 0; i < ARRAY_SIZE(sha256_k); i++)
		ctx->k[i] = sha256_k[i];

	IT8XXX2_GCTRL_SHA1HBADDR = ((uint32_t)ctx >> 6) & 0xffc;
	IT8XXX2_GCTRL_SHA2HBADDR = ((uint32_t)&ctx->k >> 6) & 0xffc;
}

static void SHA256_chip_calculation(struct sha256_ctx *ctx)
{
	volatile uint8_t hash_ctrl __unused;
	uint32_t key;

	key = irq_lock();
	IT8XXX2_GCTRL_SHA1HASHCTRLR |= BIT(1);
	hash_ctrl = IT8XXX2_GCTRL_SHA1HASHCTRLR;
	ctx->w_index = 0;
	irq_unlock(key);
}

void SHA256_update(struct sha256_ctx *ctx, const uint8_t *data, uint32_t len)
{
	uint32_t rem_len = len, data_index = 0;
	uint32_t *p = (uint32_t *)data;

	/* Requires 4-byte alignment */
	ASSERT(len % 4 == 0);

	while (rem_len) {
		ctx->w[ctx->w_index++] = htobe32(p[data_index++]);
		if (ctx->w_index >= 16) {
			SHA256_chip_calculation(ctx);
		}
		rem_len -= 4;
	}
	ctx->total_len += len;
}

void SHA256_abort(struct sha256_ctx *ctx)
{
	ARG_UNUSED(ctx);
}

uint8_t *SHA256_final(struct sha256_ctx *ctx)
{
	int i;

	memset(&ctx->w[ctx->w_index], 0, SHA256_BLOCK_SIZE - ctx->w_index * 4);
	ctx->w[ctx->w_index] = 0x80000000;

	if (ctx->w_index >= 14) {
		SHA256_chip_calculation(ctx);
		memset(&ctx->w[ctx->w_index], 0,
		       SHA256_BLOCK_SIZE - ctx->w_index * 4);
	}
	ctx->w[15] = ctx->total_len * 8;
	SHA256_chip_calculation(ctx);

	for (i = 0; i < 8; i++) {
		ctx->h[i] = be32toh(ctx->h[i]);
	}

	return (uint8_t *)ctx->h;
}
