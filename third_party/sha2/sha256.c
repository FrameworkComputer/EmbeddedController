/* SHA-256 and SHA-512 implementation based on code by Oliver Gay
 * <olivier.gay@a3.epfl.ch> under a BSD-style license. See below.
 */

/*
 * FIPS 180-2 SHA-224/256/384/512 implementation
 * Last update: 02/02/2007
 * Issue date:  04/30/2005
 *
 * Copyright (C) 2005, 2007 Olivier Gay <olivier.gay@a3.epfl.ch>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the project nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE PROJECT AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE PROJECT OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "sha256.h"
#include "util.h"

#define SHFR(x, n)    (x >> n)
#define ROTR(x, n)   ((x >> n) | (x << ((sizeof(x) << 3) - n)))
#define ROTL(x, n)   ((x << n) | (x >> ((sizeof(x) << 3) - n)))
#define CH(x, y, z)  ((x & y) ^ (~x & z))
#define MAJ(x, y, z) ((x & y) ^ (x & z) ^ (y & z))

#define SHA256_F1(x) (ROTR(x,  2) ^ ROTR(x, 13) ^ ROTR(x, 22))
#define SHA256_F2(x) (ROTR(x,  6) ^ ROTR(x, 11) ^ ROTR(x, 25))
#define SHA256_F3(x) (ROTR(x,  7) ^ ROTR(x, 18) ^ SHFR(x,  3))
#define SHA256_F4(x) (ROTR(x, 17) ^ ROTR(x, 19) ^ SHFR(x, 10))

#define UNPACK32(x, str)				\
	{						\
		*((str) + 3) = (uint8_t) ((x));		\
		*((str) + 2) = (uint8_t) ((x) >>  8);	\
		*((str) + 1) = (uint8_t) ((x) >> 16);	\
		*((str) + 0) = (uint8_t) ((x) >> 24);	\
	}

#define PACK32(str, x)						\
	{							\
		*(x) = ((uint32_t) *((str) + 3))		\
			| ((uint32_t) *((str) + 2) <<  8)	\
			| ((uint32_t) *((str) + 1) << 16)	\
			| ((uint32_t) *((str) + 0) << 24);	\
	}

/* Macros used for loops unrolling */

#define SHA256_SCR(i)						\
	{							\
		w[i] =  SHA256_F4(w[i -  2]) + w[i -  7]	\
			+ SHA256_F3(w[i - 15]) + w[i - 16];	\
	}

#define SHA256_EXP(a, b, c, d, e, f, g, h, j)				\
	{								\
		t1 = wv[h] + SHA256_F2(wv[e]) + CH(wv[e], wv[f], wv[g])	\
			+ sha256_k[j] + w[j];				\
		t2 = SHA256_F1(wv[a]) + MAJ(wv[a], wv[b], wv[c]);	\
		wv[d] += t1;						\
		wv[h] = t1 + t2;					\
	}

static const uint32_t sha256_h0[8] = {
	0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
	0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};

static const uint32_t sha256_k[64] = {
	0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
	0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
	0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
	0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
	0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
	0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
	0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
	0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
	0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
	0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
	0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
	0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
	0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
	0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
	0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
	0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

void SHA256_init(struct sha256_ctx *ctx)
{
	int i;

	for (i = 0; i < 8; i++)
		ctx->h[i] = sha256_h0[i];

	ctx->len = 0;
	ctx->tot_len = 0;
}

static void SHA256_transform(struct sha256_ctx *ctx, const uint8_t *message,
			     unsigned int block_nb)
{
	/* Note: this function requires a considerable amount of stack */
	uint32_t w[64];
	uint32_t wv[8];
	uint32_t t1, t2;
	const unsigned char *sub_block;
	int i, j;

	for (i = 0; i < (int) block_nb; i++) {
		sub_block = message + (i << 6);

		for (j = 0; j < 16; j++)
			PACK32(&sub_block[j << 2], &w[j]);

#ifdef CONFIG_SHA256_UNROLLED
		for (j = 16; j < 64; j += 8) {
			SHA256_SCR(j);
			SHA256_SCR(j+1);
			SHA256_SCR(j+2);
			SHA256_SCR(j+3);
			SHA256_SCR(j+4);
			SHA256_SCR(j+5);
			SHA256_SCR(j+6);
			SHA256_SCR(j+7);
		}
#else
		for (j = 16; j < 64; j++)
			SHA256_SCR(j);
#endif

		for (j = 0; j < 8; j++)
			wv[j] = ctx->h[j];

#ifdef CONFIG_SHA256_UNROLLED
		for (j = 0; j < 64; j += 8) {
			SHA256_EXP(0, 1, 2, 3, 4, 5, 6, 7, j);
			SHA256_EXP(7, 0, 1, 2, 3, 4, 5, 6, j+1);
			SHA256_EXP(6, 7, 0, 1, 2, 3, 4, 5, j+2);
			SHA256_EXP(5, 6, 7, 0, 1, 2, 3, 4, j+3);
			SHA256_EXP(4, 5, 6, 7, 0, 1, 2, 3, j+4);
			SHA256_EXP(3, 4, 5, 6, 7, 0, 1, 2, j+5);
			SHA256_EXP(2, 3, 4, 5, 6, 7, 0, 1, j+6);
			SHA256_EXP(1, 2, 3, 4, 5, 6, 7, 0, j+7);
		}
#else
		for (j = 0; j < 64; j++) {
			t1 = wv[7] + SHA256_F2(wv[4]) + CH(wv[4], wv[5], wv[6])
				+ sha256_k[j] + w[j];
			t2 = SHA256_F1(wv[0]) + MAJ(wv[0], wv[1], wv[2]);
			wv[7] = wv[6];
			wv[6] = wv[5];
			wv[5] = wv[4];
			wv[4] = wv[3] + t1;
			wv[3] = wv[2];
			wv[2] = wv[1];
			wv[1] = wv[0];
			wv[0] = t1 + t2;
		}
#endif

		for (j = 0; j < 8; j++)
			ctx->h[j] += wv[j];
	}
}

void SHA256_update(struct sha256_ctx *ctx, const uint8_t *data, uint32_t len)
{
	unsigned int block_nb;
	unsigned int new_len, rem_len, tmp_len;
	const uint8_t *shifted_data;

	tmp_len = SHA256_BLOCK_SIZE - ctx->len;
	rem_len = len < tmp_len ? len : tmp_len;

	memcpy(&ctx->block[ctx->len], data, rem_len);

	if (ctx->len + len < SHA256_BLOCK_SIZE) {
		ctx->len += len;
		return;
	}

	new_len = len - rem_len;
	block_nb = new_len / SHA256_BLOCK_SIZE;

	shifted_data = data + rem_len;

	SHA256_transform(ctx, ctx->block, 1);
	SHA256_transform(ctx, shifted_data, block_nb);

	rem_len = new_len % SHA256_BLOCK_SIZE;

	memcpy(ctx->block, &shifted_data[block_nb << 6], rem_len);

	ctx->len = rem_len;
	ctx->tot_len += (block_nb + 1) << 6;
}

/*
 * Specialized SHA256_init + SHA256_update that takes the first data block of
 * size SHA256_BLOCK_SIZE as input.
 */
static void SHA256_init_1b(struct sha256_ctx *ctx, const uint8_t *data)
{
	int i;

	for (i = 0; i < 8; i++)
		ctx->h[i] = sha256_h0[i];

	SHA256_transform(ctx, data, 1);

	ctx->len = 0;
	ctx->tot_len = SHA256_BLOCK_SIZE;
}

uint8_t *SHA256_final(struct sha256_ctx *ctx)
{
	unsigned int block_nb;
	unsigned int pm_len;
	unsigned int len_b;
	int i;

	block_nb = (1 + ((SHA256_BLOCK_SIZE - 9)
			 < (ctx->len % SHA256_BLOCK_SIZE)));

	len_b = (ctx->tot_len + ctx->len) << 3;
	pm_len = block_nb << 6;

	memset(ctx->block + ctx->len, 0, pm_len - ctx->len);
	ctx->block[ctx->len] = 0x80;
	UNPACK32(len_b, ctx->block + pm_len - 4);

	SHA256_transform(ctx, ctx->block, block_nb);

	for (i = 0; i < 8; i++)
		UNPACK32(ctx->h[i], &ctx->buf[i << 2]);

	return ctx->buf;
}

static void hmac_SHA256_step(uint8_t *output, uint8_t mask,
			const uint8_t *key, const int key_len,
			const uint8_t *data, const int data_len) {
	struct sha256_ctx ctx;
	uint8_t *key_pad = ctx.block;
	uint8_t *tmp;
	int i;

	/* key_pad = key (zero-padded) ^ mask */
	memset(key_pad, mask, SHA256_BLOCK_SIZE);
	for (i = 0; i < key_len; i++)
		key_pad[i] ^= key[i];

	/* tmp = hash(key_pad || message) */
	SHA256_init_1b(&ctx, key_pad);
	SHA256_update(&ctx, data, data_len);
	tmp = SHA256_final(&ctx);
	memcpy(output, tmp, SHA256_DIGEST_SIZE);
}

void hmac_SHA256(uint8_t *output, const uint8_t *key, const int key_len,
		 const uint8_t *message, const int message_len) {
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
	hmac_SHA256_step(output, 0x5c,
			 key, key_len, output, SHA256_DIGEST_SIZE);
}
