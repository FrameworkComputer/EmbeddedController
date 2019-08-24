/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "console.h"
#include "cryptoc/util.h"
#include "dcrypto.h"
#include "internal.h"
#include "trng.h"

/* HMAC_DRBG flow in NIST SP 800-90Ar1, 10.2, RFC 6979
 */
/* V = HMAC(K, V) */
static void update_v(const uint32_t *k, uint32_t *v)
{
	LITE_HMAC_CTX ctx;

	DCRYPTO_HMAC_SHA256_init(&ctx, k, SHA256_DIGEST_SIZE);
	HASH_update(&ctx.hash, v, SHA256_DIGEST_SIZE);
	memcpy(v, DCRYPTO_HMAC_final(&ctx), SHA256_DIGEST_SIZE);
}

/* K = HMAC(K, V || tag || p0 || p1 || p2) */
/* V = HMAC(K, V) */
static void update_kv(uint32_t *k, uint32_t *v, uint8_t tag,
		      const void *p0, size_t p0_len,
		      const void *p1, size_t p1_len,
		      const void *p2, size_t p2_len)
{
	LITE_HMAC_CTX ctx;

	DCRYPTO_HMAC_SHA256_init(&ctx, k, SHA256_DIGEST_SIZE);
	HASH_update(&ctx.hash, v, SHA256_DIGEST_SIZE);
	HASH_update(&ctx.hash, &tag, 1);
	HASH_update(&ctx.hash, p0, p0_len);
	HASH_update(&ctx.hash, p1, p1_len);
	HASH_update(&ctx.hash, p2, p2_len);
	memcpy(k, DCRYPTO_HMAC_final(&ctx), SHA256_DIGEST_SIZE);

	update_v(k, v);
}

static void update(struct drbg_ctx *ctx,
		   const void *p0, size_t p0_len,
		   const void *p1, size_t p1_len,
		   const void *p2, size_t p2_len)
{
	/* K = HMAC(K, V || 0x00 || provided_data) */
	/* V = HMAC(K, V) */
	update_kv(ctx->k, ctx->v, 0x00,
		  p0, p0_len, p1, p1_len, p2, p2_len);

	/* If no provided_data, stop. */
	if (p0_len + p1_len + p2_len == 0)
		return;

	/* K = HMAC(K, V || 0x01 || provided_data) */
	/* V = HMAC(K, V) */
	update_kv(ctx->k, ctx->v,
		  0x01,
		  p0, p0_len, p1, p1_len, p2, p2_len);
}

void hmac_drbg_init(struct drbg_ctx *ctx,
		    const void *p0, size_t p0_len,
		    const void *p1, size_t p1_len,
		    const void *p2, size_t p2_len)
{
	/* K = 0x00 0x00 0x00 ... 0x00 */
	always_memset(ctx->k,  0x00, sizeof(ctx->k));
	/* V = 0x01 0x01 0x01 ... 0x01 */
	always_memset(ctx->v,  0x01, sizeof(ctx->v));

	update(ctx, p0, p0_len, p1, p1_len, p2, p2_len);

	ctx->reseed_counter = 1;
}

void hmac_drbg_init_rfc6979(struct drbg_ctx *ctx, const p256_int *key,
			    const p256_int *message)
{
	hmac_drbg_init(ctx,
		       key->a, sizeof(key->a),
		       message->a, sizeof(message->a),
		       NULL, 0);
}

void hmac_drbg_init_rand(struct drbg_ctx *ctx, size_t nbits)
{
	int i;
	uint32_t x[(nbits + 31) / 32];

	for (i = 0; i < ARRAY_SIZE(x); ++i)
		x[i] = rand();

	hmac_drbg_init(ctx, &x, sizeof(x), NULL, 0, NULL, 0);
}

void hmac_drbg_reseed(struct drbg_ctx *ctx,
		      const void *p0, size_t p0_len,
		      const void *p1, size_t p1_len,
		      const void *p2, size_t p2_len)
{
	update(ctx, p0, p0_len, p1, p1_len, p2, p2_len);
	ctx->reseed_counter = 1;
}

int hmac_drbg_generate(struct drbg_ctx *ctx,
		       void *out, size_t out_len,
		       const void *input, size_t input_len)
{
	/* TODO(louiscollard): Assert maximum output length? */

	if (ctx->reseed_counter >= 10000)
		return 2;

	if (input_len)
		update(ctx, input, input_len, NULL, 0, NULL, 0);

	while (out_len) {
		size_t n = out_len > sizeof(ctx->v) ? sizeof(ctx->v) : out_len;

		update_v(ctx->k, ctx->v);

		memcpy(out, ctx->v, n);
		out += n;
		out_len -= n;
	}

	update(ctx, input, input_len, NULL, 0, NULL, 0);
	ctx->reseed_counter++;

	return 0;
}

void hmac_drbg_generate_p256(struct drbg_ctx *ctx, p256_int *k_out)
{
	hmac_drbg_generate(ctx,
			   k_out->a, sizeof(k_out->a),
			   NULL, 0);
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

	hmac_drbg_init_rfc6979(&drbg, x, &h1);
	do {
		hmac_drbg_generate_p256(&drbg, &k);
		ccprintf("K = %.32h\n", &k);
	} while (p256_cmp(&SECP256r1_nMin2, &k) < 0);
	drbg_exit(&drbg);
	result = p256_cmp(&k, reference_k);
	ccprintf("K generation: %s\n", result ? "FAIL" : "PASS");

	return result ? EC_ERROR_INVAL : EC_SUCCESS;
}
DECLARE_SAFE_CONSOLE_COMMAND(rfc6979, cmd_rfc6979, NULL, NULL);

/*
 * Test vectors from the NIST Cryptographic Algorithm Validation Program.
 *
 * These are the first two examples from the SHA-256, without prediction
 * resistance, and with reseed supported.
 */
#define HMAC_TEST_COUNT 2
static int cmd_hmac_drbg(int argc, char **argv)
{
	static struct drbg_ctx ctx;

	static const uint8_t init_entropy[HMAC_TEST_COUNT][32] = {
		{
			0x06, 0x03, 0x2C, 0xD5, 0xEE, 0xD3, 0x3F, 0x39, 0x26,
			0x5F, 0x49, 0xEC, 0xB1, 0x42, 0xC5, 0x11, 0xDA, 0x9A,
			0xFF, 0x2A, 0xF7, 0x12, 0x03, 0xBF, 0xFA, 0xF3, 0x4A,
			0x9C, 0xA5, 0xBD, 0x9C, 0x0D
		},
		{
			0xAA, 0xDC, 0xF3, 0x37, 0x78, 0x8B, 0xB8, 0xAC, 0x01,
			0x97, 0x66, 0x40, 0x72, 0x6B, 0xC5, 0x16, 0x35, 0xD4,
			0x17, 0x77, 0x7F, 0xE6, 0x93, 0x9E, 0xDE, 0xD9, 0xCC,
			0xC8, 0xA3, 0x78, 0xC7, 0x6A
		},
	};

	static const uint8_t init_nonce[HMAC_TEST_COUNT][16] = {
		{
			0x0E, 0x66, 0xF7, 0x1E, 0xDC, 0x43, 0xE4, 0x2A, 0x45,
			0xAD, 0x3C, 0x6F, 0xC6, 0xCD, 0xC4, 0xDF
		},
		{
			0x9C, 0xCC, 0x9D, 0x80, 0xC8, 0x9A, 0xC5, 0x5A, 0x8C,
			0xFE, 0x0F, 0x99, 0x94, 0x2F, 0x5A, 0x4D
		},
	};

	static const uint8_t reseed_entropy[HMAC_TEST_COUNT][32] = {
		{
			0x01, 0x92, 0x0A, 0x4E, 0x66, 0x9E, 0xD3, 0xA8, 0x5A,
			0xE8, 0xA3, 0x3B, 0x35, 0xA7, 0x4A, 0xD7, 0xFB, 0x2A,
			0x6B, 0xB4, 0xCF, 0x39, 0x5C, 0xE0, 0x03, 0x34, 0xA9,
			0xC9, 0xA5, 0xA5, 0xD5, 0x52
		},
		{
			0x03, 0xA5, 0x77, 0x92, 0x54, 0x7E, 0x0C, 0x98, 0xEA,
			0x17, 0x76, 0xE4, 0xBA, 0x80, 0xC0, 0x07, 0x34, 0x62,
			0x96, 0xA5, 0x6A, 0x27, 0x0A, 0x35, 0xFD, 0x9E, 0xA2,
			0x84, 0x5C, 0x7E, 0x81, 0xE2
		}
	};

	static const uint8_t expected_output[HMAC_TEST_COUNT][128] = {
		{
			0x76, 0xFC, 0x79, 0xFE, 0x9B, 0x50, 0xBE, 0xCC, 0xC9,
			0x91, 0xA1, 0x1B, 0x56, 0x35, 0x78, 0x3A, 0x83, 0x53,
			0x6A, 0xDD, 0x03, 0xC1, 0x57, 0xFB, 0x30, 0x64, 0x5E,
			0x61, 0x1C, 0x28, 0x98, 0xBB, 0x2B, 0x1B, 0xC2, 0x15,
			0x00, 0x02, 0x09, 0x20, 0x8C, 0xD5, 0x06, 0xCB, 0x28,
			0xDA, 0x2A, 0x51, 0xBD, 0xB0, 0x38, 0x26, 0xAA, 0xF2,
			0xBD, 0x23, 0x35, 0xD5, 0x76, 0xD5, 0x19, 0x16, 0x08,
			0x42, 0xE7, 0x15, 0x8A, 0xD0, 0x94, 0x9D, 0x1A, 0x9E,
			0xC3, 0xE6, 0x6E, 0xA1, 0xB1, 0xA0, 0x64, 0xB0, 0x05,
			0xDE, 0x91, 0x4E, 0xAC, 0x2E, 0x9D, 0x4F, 0x2D, 0x72,
			0xA8, 0x61, 0x6A, 0x80, 0x22, 0x54, 0x22, 0x91, 0x82,
			0x50, 0xFF, 0x66, 0xA4, 0x1B, 0xD2, 0xF8, 0x64, 0xA6,
			0xA3, 0x8C, 0xC5, 0xB6, 0x49, 0x9D, 0xC4, 0x3F, 0x7F,
			0x2B, 0xD0, 0x9E, 0x1E, 0x0F, 0x8F, 0x58, 0x85, 0x93,
			0x51, 0x24
		},
		{
			0x17, 0xD0, 0x9F, 0x40, 0xA4, 0x37, 0x71, 0xF4, 0xA2,
			0xF0, 0xDB, 0x32, 0x7D, 0xF6, 0x37, 0xDE, 0xA9, 0x72,
			0xBF, 0xFF, 0x30, 0xC9, 0x8E, 0xBC, 0x88, 0x42, 0xDC,
			0x7A, 0x9E, 0x3D, 0x68, 0x1C, 0x61, 0x90, 0x2F, 0x71,
			0xBF, 0xFA, 0xF5, 0x09, 0x36, 0x07, 0xFB, 0xFB, 0xA9,
			0x67, 0x4A, 0x70, 0xD0, 0x48, 0xE5, 0x62, 0xEE, 0x88,
			0xF0, 0x27, 0xF6, 0x30, 0xA7, 0x85, 0x22, 0xEC, 0x6F,
			0x70, 0x6B, 0xB4, 0x4A, 0xE1, 0x30, 0xE0, 0x5C, 0x8D,
			0x7E, 0xAC, 0x66, 0x8B, 0xF6, 0x98, 0x0D, 0x99, 0xB4,
			0xC0, 0x24, 0x29, 0x46, 0x45, 0x23, 0x99, 0xCB, 0x03,
			0x2C, 0xC6, 0xF9, 0xFD, 0x96, 0x28, 0x47, 0x09, 0xBD,
			0x2F, 0xA5, 0x65, 0xB9, 0xEB, 0x9F, 0x20, 0x04, 0xBE,
			0x6C, 0x9E, 0xA9, 0xFF, 0x91, 0x28, 0xC3, 0xF9, 0x3B,
			0x60, 0xDC, 0x30, 0xC5, 0xFC, 0x85, 0x87, 0xA1, 0x0D,
			0xE6, 0x8C
		}
	};

	static uint8_t output[128];

	int i, cmp_result;

	for (i = 0; i < HMAC_TEST_COUNT; i++) {
		hmac_drbg_init(&ctx,
			       init_entropy[i], sizeof(init_entropy[i]),
			       init_nonce[i], sizeof(init_nonce[i]),
			       NULL, 0);

		hmac_drbg_reseed(&ctx,
				 reseed_entropy[i], sizeof(reseed_entropy[i]),
				 NULL, 0,
				 NULL, 0);

		hmac_drbg_generate(&ctx,
				   output, sizeof(output),
				   NULL, 0);

		hmac_drbg_generate(&ctx,
				   output, sizeof(output),
				   NULL, 0);

		cmp_result = memcmp(output, expected_output[i], sizeof(output));
		ccprintf("HMAC DRBG generate test %d, %s\n",
			 i, cmp_result ? "failed" : "passed");
	}

	return 0;
}
DECLARE_SAFE_CONSOLE_COMMAND(hmac_drbg, cmd_hmac_drbg, NULL, NULL);

/*
 * Sanity check to exercise random initialization.
 */
static int cmd_hmac_drbg_rand(int argc, char **argv)
{
	static struct drbg_ctx ctx;
	static uint8_t output[128];

	int i;

	hmac_drbg_init_rand(&ctx, 256);

	hmac_drbg_generate(&ctx, output, sizeof(output), NULL, 0);

	ccprintf("Randomly initialized HMAC DRBG, 1024 bit output: ");

	for (i = 0; i < sizeof(output); i++)
		ccprintf("%x", output[i]);
	ccprintf("\n");

	return 0;
}
DECLARE_SAFE_CONSOLE_COMMAND(hmac_drbg_rand, cmd_hmac_drbg_rand, NULL, NULL);
#endif /* CRYPTO_TEST_SETUP */
