/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "dcrypto.h"
#include "internal.h"
#include "registers.h"

#include "endian.h"

#include "cryptoc/util.h"

static void gcm_mul(uint32_t *counter)
{
	int i;
	volatile uint32_t *p;

	/* Set HASH to zero. */
	p = GREG32_ADDR(KEYMGR, GCM_HASH_IN0);
	for (i = 0; i < 4; i++)
		*p++ = 0;

	/* Initialize GMAC. */
	p = GREG32_ADDR(KEYMGR, GCM_MAC0);
	for (i = 0; i < 4; i++)
		*p++ = counter[i];

	/* Crank GMAC. */
	GREG32(KEYMGR, GCM_DO_ACC) = 1;

	/* Read GMAC. */
	p = GREG32_ADDR(KEYMGR, GCM_MAC0);
	for (i = 0; i < 4; i++)
		counter[i] = *p++;

	/* Reset GMAC. */
	p = GREG32_ADDR(KEYMGR, GCM_MAC0);
	for (i = 0; i < 4; ++i)
		*p++ = 0;
}

static void gcm_init_iv(
	const uint8_t *iv, uint32_t iv_len, uint32_t *counter)
{

	if (iv_len == 12) {
		memcpy(counter, iv, 12);
		counter[3] = BIT(24);
	} else {
		size_t i;
		uint32_t len = iv_len;
		uint64_t len0 = len;
		uint8_t *ctr = (uint8_t *) counter;

		memset(ctr, 0, 16);
		while (len >= 16) {
			for (i = 0; i < 16; ++i)
				ctr[i] ^= iv[i];

			gcm_mul(counter);
			iv += 16;
			len -= 16;
		}
		if (len) {
			for (i = 0; i < len; ++i)
				ctr[i] ^= iv[i];

			gcm_mul(counter);
		}
		len0 <<= 3;
		ctr[8] ^= (uint8_t)(len0 >> 56);
		ctr[9] ^= (uint8_t)(len0 >> 48);
		ctr[10] ^= (uint8_t)(len0 >> 40);
		ctr[11] ^= (uint8_t)(len0 >> 32);
		ctr[12] ^= (uint8_t)(len0 >> 24);
		ctr[13] ^= (uint8_t)(len0 >> 16);
		ctr[14] ^= (uint8_t)(len0 >> 8);
		ctr[15] ^= (uint8_t)(len0);

		gcm_mul(counter);
	}
}

void DCRYPTO_gcm_init(struct GCM_CTX *ctx, const uint8_t *key,
		const uint8_t *iv, size_t iv_len)
{
	int i;
	const uint32_t zero[4] = {0, 0, 0, 0};
	uint32_t H[4];
	uint32_t counter[4];

	memset(ctx, 0, sizeof(struct GCM_CTX));

	/* Initialize AES engine in CTR mode, and set the counter to 0. */
	DCRYPTO_aes_init(key, 128, (const uint8_t *) zero,
			 CIPHER_MODE_CTR, ENCRYPT_MODE);
	/* Set H to AES(ZERO). */
	DCRYPTO_aes_block((const uint8_t *) zero, (uint8_t *) H);

	/* Initialize the GMAC accumulator to ZERO. */
	for (i = 0; i < 4; i++)
		GR_KEYMGR_GCM_MAC(i) = zero[i];

	/* Initialize H. */
	for (i = 0; i < 4; i++)
		GR_KEYMGR_GCM_H(i) = H[i];

	/* Map the IV to a 128-bit counter. */
	gcm_init_iv(iv, iv_len, counter);

	/* Re-initialize the IV counter. */
	for (i = 0; i < 4; i++)
		GR_KEYMGR_AES_CTR(i) = counter[i];

	/* Calculate Ej0: encrypt IV counter XOR ZERO. */
	DCRYPTO_aes_block((const uint8_t *) zero, ctx->Ej0.c);
}

static void gcm_aad_block(const struct GCM_CTX *ctx, const uint32_t *block)
{
	int i;
	const struct access_helper *p = (struct access_helper *) block;

	if (ctx->aad_len == 0 && ctx->count <= 16) {
		/* Update GMAC. */
		for (i = 0; i < 4; i++)
			GR_KEYMGR_GCM_MAC(i) = p[i].udata;
	} else {
		for (i = 0; i < 4; i++)
			GR_KEYMGR_GCM_HASH_IN(i) = p[i].udata;

		/* Crank GMAC. */
		GREG32(KEYMGR, GCM_DO_ACC) = 1;
	}
}

void DCRYPTO_gcm_aad(struct GCM_CTX *ctx, const uint8_t *aad_data, size_t len)
{
	uint32_t block[4];

	while (len) {
		size_t count;

		memset(block, 0, sizeof(block));
		count = MIN(16, len);
		memcpy(block, aad_data, count);

		gcm_aad_block(ctx, block);
		ctx->aad_len += count;

		len -= count;
		aad_data += count;
	}

	always_memset(block, 0, sizeof(block));
}

int DCRYPTO_gcm_encrypt(struct GCM_CTX *ctx, uint8_t *out, size_t out_len,
			const uint8_t *in, size_t in_len)
{
	uint8_t *outp = out;

	if (out_len < (in_len & ~0x0F) + ((in_len & 0x0F) ? 16 : 0))
		return -1;

	/* Process a previous partial block, if any. */
	if (ctx->remainder) {
		size_t count = MIN(in_len, 16 - ctx->remainder);

		memcpy(ctx->block.c + ctx->remainder, in, count);
		ctx->remainder += count;
		if (ctx->remainder < 16)
			return 0;

		DCRYPTO_aes_block(ctx->block.c, outp);
		ctx->count += 16;
		gcm_aad_block(ctx, (uint32_t *) outp);
		ctx->remainder = 0;
		in += count;
		in_len -= count;
		outp += 16;
	}

	while (in_len >= 16) {
		DCRYPTO_aes_block(in, outp);
		ctx->count += 16;

		gcm_aad_block(ctx, (uint32_t *) outp);

		in_len -= 16;
		in += 16;
		outp += 16;
	}

	if (in_len) {
		memcpy(ctx->block.c, in, in_len);
		ctx->remainder = in_len;
	}

	return outp - out;
}

int DCRYPTO_gcm_encrypt_final(struct GCM_CTX *ctx, uint8_t *out, size_t out_len)
{
	if (out_len < ctx->remainder)
		return -1;

	if (ctx->remainder) {
		size_t remainder = ctx->remainder;
		uint8_t out_block[16];

		DCRYPTO_aes_block(ctx->block.c, out_block);
		ctx->count += ctx->remainder;
		memcpy(out, out_block, ctx->remainder);

		memset(out_block + ctx->remainder, 0, 16 - ctx->remainder);
		gcm_aad_block(ctx, (uint32_t *) out_block);
		ctx->remainder = 0;
		return remainder;
	}

	return 0;
}

int DCRYPTO_gcm_decrypt(struct GCM_CTX *ctx, uint8_t *out, size_t out_len,
			const uint8_t *in, size_t in_len)
{
	uint8_t *outp = out;

	if (out_len < (in_len & ~0x0F) + ((in_len & 0x0F) ? 16 : 0))
		return -1;

	if (ctx->remainder) {
		size_t count = MIN(in_len, 16 - ctx->remainder);

		memcpy(ctx->block.c + ctx->remainder, in, count);
		ctx->remainder += count;

		if (ctx->remainder < 16)
			return 0;

		DCRYPTO_aes_block(ctx->block.c, outp);
		ctx->remainder = 0;
		ctx->count += 16;
		gcm_aad_block(ctx, ctx->block.d);
		in += count;
		in_len -= count;
		outp += count;
	}

	while (in_len >= 16) {
		DCRYPTO_aes_block(in, outp);
		ctx->count += 16;
		gcm_aad_block(ctx, (uint32_t *) in);
		in += 16;
		in_len -= 16;
		outp += 16;
	}

	if (in_len) {
		memcpy(ctx->block.c, in, in_len);
		ctx->remainder = in_len;
	}

	return outp - out;
}

int DCRYPTO_gcm_decrypt_final(struct GCM_CTX *ctx,
			uint8_t *out, size_t out_len)
{
	if (out_len < ctx->remainder)
		return -1;

	if (ctx->remainder) {
		size_t remainder = ctx->remainder;
		uint8_t out_block[16];

		DCRYPTO_aes_block(ctx->block.c, out_block);
		ctx->count += ctx->remainder;
		memcpy(out, out_block, ctx->remainder);

		memset(ctx->block.c + ctx->remainder, 0, 16 - ctx->remainder);
		gcm_aad_block(ctx, ctx->block.d);
		ctx->remainder = 0;
		return remainder;
	}

	return 0;
}

static void dcrypto_gcm_len_vector(
	const struct GCM_CTX *ctx, void *len_vector) {
	uint64_t aad_be;
	uint64_t count_be;

	/* Serialize counters to bit-count (big-endian). */
	aad_be = ctx->aad_len * 8;
	aad_be = htobe64(aad_be);
	count_be = ctx->count * 8;
	count_be = htobe64(count_be);

	memcpy(len_vector, &aad_be, 8);
	memcpy(((uint8_t *)len_vector) + 8, &count_be, 8);
}

static void dcrypto_gcm_tag(const struct GCM_CTX *ctx,
			const uint32_t *len_vector, uint32_t *tag) {
	int i;

	for (i = 0; i < 4; i++)
		GR_KEYMGR_GCM_HASH_IN(i) = len_vector[i];

	/* Crank GMAC. */
	GREG32(KEYMGR, GCM_DO_ACC) = 1;

	for (i = 0; i < 4; i++)
		GR_KEYMGR_GCM_HASH_IN(i) = ctx->Ej0.d[i];

	/* Crank GMAC. */
	GREG32(KEYMGR, GCM_DO_ACC) = 1;

	/* Read tag. */
	for (i = 0; i < 4; i++)
		tag[i] = GR_KEYMGR_GCM_MAC(i);
}

int DCRYPTO_gcm_tag(struct GCM_CTX *ctx, uint8_t *tag, size_t tag_len)
{
	uint32_t len_vector[4];
	uint32_t local_tag[4];
	size_t count = MIN(tag_len, sizeof(local_tag));

	dcrypto_gcm_len_vector(ctx, len_vector);
	dcrypto_gcm_tag(ctx, len_vector, local_tag);

	memcpy(tag, local_tag, count);
	return count;
}

void DCRYPTO_gcm_finish(struct GCM_CTX *ctx)
{
	always_memset(ctx, 0, sizeof(struct GCM_CTX));
	GREG32(KEYMGR, AES_WIPE_SECRETS) = 1;
}
