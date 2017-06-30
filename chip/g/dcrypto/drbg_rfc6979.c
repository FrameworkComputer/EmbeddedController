/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "console.h"
#include "cryptoc/util.h"
#include "dcrypto.h"
#include "internal.h"
#include "trng.h"

/* V = HMAC_K(V) */
static void update_v(const uint32_t *k, uint32_t *v)
{
	LITE_HMAC_CTX ctx;

	DCRYPTO_HMAC_SHA256_init(&ctx, k, SHA256_DIGEST_SIZE);
	HASH_update(&ctx.hash, v, SHA256_DIGEST_SIZE);
	memcpy(v, DCRYPTO_HMAC_final(&ctx), SHA256_DIGEST_SIZE);
}

/* K = HMAC_K(V || tag || x || h1) */
static void update_k(uint32_t *k, const uint32_t *v, uint8_t tag,
		     const uint32_t *x,  const uint32_t *h1)
{
	LITE_HMAC_CTX ctx;

	DCRYPTO_HMAC_SHA256_init(&ctx, k, SHA256_DIGEST_SIZE);
	HASH_update(&ctx.hash, v, SHA256_DIGEST_SIZE);
	HASH_update(&ctx.hash, &tag, 1);
	HASH_update(&ctx.hash, x, SHA256_DIGEST_SIZE);
	HASH_update(&ctx.hash, h1, SHA256_DIGEST_SIZE);
	memcpy(k, DCRYPTO_HMAC_final(&ctx), SHA256_DIGEST_SIZE);
}

/* K = HMAC_K(V || 0x00) */
static void append_0(uint32_t *k, const uint32_t *v)
{
	LITE_HMAC_CTX ctx;
	uint8_t zero = 0;

	DCRYPTO_HMAC_SHA256_init(&ctx, k, SHA256_DIGEST_SIZE);
	HASH_update(&ctx.hash, v, SHA256_DIGEST_SIZE);
	HASH_update(&ctx.hash, &zero, 1);
	memcpy(k, DCRYPTO_HMAC_final(&ctx), SHA256_DIGEST_SIZE);
}

/* Deterministic generation of k as per RFC 6979 */
void drbg_rfc6979_init(struct drbg_ctx *ctx, const p256_int *key,
		       const p256_int *message)
{
	const uint32_t *x = key->a;
	const uint32_t *h1 = message->a;

	/* V = 0x01 0x01 0x01 ... 0x01 */
	always_memset(ctx->v,  0x01, sizeof(ctx->v));
	/* K = 0x00 0x00 0x00 ... 0x00 */
	always_memset(ctx->k,  0x00, sizeof(ctx->k));
	/* K = HMAC_K(V || 0x00 || int2octets(x) || bits2octets(h1)) */
	update_k(ctx->k, ctx->v, 0x00, x, h1);
	/* V = HMAC_K(V) */
	update_v(ctx->k, ctx->v);
	/* K = HMAC_K(V || 0x01 || int2octets(x) || bits2octets(h1)) */
	update_k(ctx->k, ctx->v, 0x01, x, h1);
	/* V = HMAC_K(V) */
	update_v(ctx->k, ctx->v);
}

void drbg_rand_init(struct drbg_ctx *ctx)
{
	int i;
	p256_int x, h1;

	for (i = 0; i < P256_NDIGITS; ++i) {
		x.a[i] = rand();
		h1.a[i] = rand();
	}

	drbg_rfc6979_init(ctx, &x, &h1);
}

void drbg_generate(struct drbg_ctx *ctx, p256_int *k_out)
{
	int i;

	/* V = HMAC_K(V) */
	update_v(ctx->k, ctx->v);
	/* get the current candidate K, then prepare for the next one */
	for (i = 0; i < P256_NDIGITS; ++i)
		k_out->a[i] = ctx->v[i];
	/* K = HMAC_K(V || 0x00) */
	append_0(ctx->k, ctx->v);
	/* V = HMAC_K(V) */
	update_v(ctx->k, ctx->v);
}

void drbg_exit(struct drbg_ctx *ctx)
{
	always_memset(ctx->k,  0x00, sizeof(ctx->k));
	always_memset(ctx->v,  0x00, sizeof(ctx->v));
}

#ifdef CRYPTO_TEST_SETUP

/*
 * from the RFC 6979 A.2.5 example:
 *
 * curve: NIST P-256
 *
 * q = FFFFFFFF00000000FFFFFFFFFFFFFFFFBCE6FAADA7179E84F3B9CAC2FC632551
 * (qlen = 256 bits)
 *
 * private key:
 * x = C9AFA9D845BA75166B5C215767B1D6934E50C3DB36E89B127B8A622B120F6721
 *
 * public key: U = xG
 * Ux = 60FED4BA255A9D31C961EB74C6356D68C049B8923B61FA6CE669622E60F29FB6
 * Uy = 7903FE1008B8BC99A41AE9E95628BC64F2F1B20C2D7E9F5177A3C294D4462299
 *
 * Signature:
 * With SHA-256, message = "sample":
 * k = A6E3C57DD01ABE90086538398355DD4C3B17AA873382B0F24D6129493D8AAD60
 * r = EFD48B2AACB6A8FD1140DD9CD45E81D69D2C877B56AAF991C34D0EA84EAF3716
 * s = F7CB1C942D657C41D436C7A1B6E29F65F3E900DBB9AFF4064DC4AB2F843ACDA8
 */
static int cmd_rfc6979(int argc, char **argv)
{
	static p256_int h1;
	static p256_int k;
	static const char message[] = "sample";
	static struct drbg_ctx drbg;
	static HASH_CTX ctx;
	int result;
	static const uint8_t priv_from_rfc[] = {
		0xC9, 0xAF, 0xA9, 0xD8, 0x45, 0xBA, 0x75, 0x16,
		0x6B, 0x5C, 0x21, 0x57, 0x67, 0xB1, 0xD6, 0x93,
		0x4E, 0x50, 0xC3, 0xDB, 0x36, 0xE8, 0x9B, 0x12,
		0x7B, 0x8A, 0x62, 0x2B, 0x12, 0x0F, 0x67, 0x21
	};
	static const uint8_t k_from_rfc[] = {
		0xA6, 0xE3, 0xC5, 0x7D, 0xD0, 0x1A, 0xBE, 0x90,
		0x08, 0x65, 0x38, 0x39, 0x83, 0x55, 0xDD, 0x4C,
		0x3B, 0x17, 0xAA, 0x87, 0x33, 0x82, 0xB0, 0xF2,
		0x4D, 0x61, 0x29, 0x49, 0x3D, 0x8A, 0xAD, 0x60
	};
	p256_int *x = (p256_int *)priv_from_rfc;
	p256_int *reference_k = (p256_int *)k_from_rfc;

	/* h1 = H(m) */
	DCRYPTO_SHA256_init(&ctx, 1);
	HASH_update(&ctx, message, sizeof(message) - 1);
	memcpy(&h1, HASH_final(&ctx), SHA256_DIGEST_SIZE);

	drbg_rfc6979_init(&drbg, x, &h1);
	do {
		drbg_generate(&drbg, &k);
		ccprintf("K = %.32h\n", &k);
	} while (p256_cmp(&SECP256r1_nMin2, &k) < 0);
	drbg_exit(&drbg);
	result = p256_cmp(&k, reference_k);
	ccprintf("K generation: %s\n", result ? "FAIL" : "PASS");

	return result ? EC_ERROR_INVAL : EC_SUCCESS;
}
DECLARE_SAFE_CONSOLE_COMMAND(rfc6979, cmd_rfc6979, NULL, NULL);
#endif /* CRYPTO_TEST_SETUP */
