/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * Crypto wrapper library for the g chip.
 */
#ifndef __EC_CHIP_G_DCRYPTO_DCRYPTO_H
#define __EC_CHIP_G_DCRYPTO_DCRYPTO_H

/* TODO(vbendeb) don't forget to disable this for prod builds. */
#define CRYPTO_TEST_SETUP

#include "internal.h"

enum cipher_mode {
	CIPHER_MODE_ECB = 0,
	CIPHER_MODE_CTR = 1,
	CIPHER_MODE_CBC = 2,
	CIPHER_MODE_GCM = 3
};

enum encrypt_mode {
	DECRYPT_MODE = 0,
	ENCRYPT_MODE = 1
};

struct HASH_CTX;   /* Forward declaration. */

typedef struct HASH_CTX SHA1_CTX;
typedef struct HASH_CTX SHA256_CTX;

enum hashing_mode {
	HASH_SHA1 = 0,
	HASH_SHA256 = 1
};

#define DCRYPTO_HASH_update(ctx, data, len) \
	((ctx)->vtab->update((ctx), (data), (len)))
#define DCRYPTO_HASH_final(ctx) \
	((ctx)->vtab->final((ctx)))
#define DCRYPTO_HASH_size(ctx) \
	((ctx)->vtab->size)

#define DCRYPTO_SHA1_update(ctx, data, n) \
	DCRYPTO_HASH_update((ctx), (data), (n))
#define DCRYPTO_SHA1_final(ctx) DCRYPTO_HASH_final((ctx))

/*
 * AES implementation, based on a hardware AES block.
 */
int DCRYPTO_aes_init(const uint8_t *key, uint32_t key_len, const uint8_t *iv,
		enum cipher_mode c_mode, enum encrypt_mode e_mode);
int DCRYPTO_aes_block(const uint8_t *in, uint8_t *out);

void DCRYPTO_aes_write_iv(const uint8_t *iv);
void DCRYPTO_aes_read_iv(uint8_t *iv);

/*
 * SHA implementation.  This abstraction is backed by either a
 * software or hardware implementation.
 *
 * There could be only a single hardware SHA context in progress. The init
 * functions will try using the HW context, if available, unless 'sw_required'
 * is TRUE, in which case there will be no attempt to use the hardware for
 * this particular hashing session.
 */
void DCRYPTO_SHA1_init(SHA1_CTX *ctx, uint32_t sw_required);
void DCRYPTO_SHA256_init(SHA256_CTX *ctx, uint32_t sw_required);
const uint8_t *DCRYPTO_SHA1_hash(const uint8_t *data, uint32_t n,
				uint8_t *digest);

#define DCRYPTO_SHA256_update(ctx, data, n) \
	DCRYPTO_HASH_update((ctx), (data), (n))
#define DCRYPTO_SHA256_final(ctx) DCRYPTO_HASH_final((ctx))
const uint8_t *DCRYPTO_SHA256_hash(const uint8_t *data, uint32_t n,
				uint8_t *digest);

/*
 *  RSA.
 */

/* Largest supported key size, 2048-bits. */
#define RSA_MAX_BYTES   256
#define RSA_MAX_WORDS   (RSA_MAX_BYTES / sizeof(uint32_t))

struct RSA {
	uint32_t e;
	struct BIGNUM N;
	struct BIGNUM d;
};

enum padding_mode {
	PADDING_MODE_PKCS1 = 0,
	PADDING_MODE_OAEP  = 1,
	PADDING_MODE_PSS = 2,
	/* USE OF NULL PADDING IS NOT RECOMMENDED.
	 * SUPPORT EXISTS AS A REQUIREMENT FOR TPM2 OPERATION. */
	PADDING_MODE_NULL  = 3
};

/* Calculate r = m ^ e mod N */
int DCRYPTO_rsa_encrypt(struct RSA *rsa, uint8_t *out, uint32_t *out_len,
			const uint8_t *in, uint32_t in_len,
			enum padding_mode padding, enum hashing_mode hashing,
			const char *label);

/* Calculate r = m ^ d mod N */
int DCRYPTO_rsa_decrypt(struct RSA *rsa, uint8_t *out, uint32_t *out_len,
			const uint8_t *in, const uint32_t in_len,
			enum padding_mode padding, enum hashing_mode hashing,
			const char *label);

/* Calculate r = m ^ d mod N */
int DCRYPTO_rsa_sign(struct RSA *rsa, uint8_t *out, uint32_t *out_len,
		const uint8_t *in, const uint32_t in_len,
		enum padding_mode padding, enum hashing_mode hashing);

/* Calculate r = m ^ e mod N */
int DCRYPTO_rsa_verify(struct RSA *rsa, const uint8_t *digest,
		uint32_t digest_len, const uint8_t *sig,
		const uint32_t sig_len,	enum padding_mode padding,
		enum hashing_mode hashing);

/*
 *  EC.
 */
int DCRYPTO_p256_valid_point(const p256_int *x, const p256_int *y);
int DCRYPTO_p256_base_point_mul(p256_int *out_x, p256_int *out_y,
				const p256_int *n);
int DCRYPTO_p256_points_mul(p256_int *out_x, p256_int *out_y,
			const p256_int *n1, const p256_int *n2,
			const p256_int *in_x, const p256_int *in_y);
int DCRYPTO_p256_key_from_bytes(p256_int *x, p256_int *y, p256_int *d,
				const uint8_t key_bytes[P256_NBYTES]);

void DCRYPTO_p256_ecdsa_sign(const p256_int *d, const p256_int *digest,
			p256_int *r, p256_int *s);
int DCRYPTO_p256_ecdsa_verify(const p256_int *key_x, const p256_int *key_y,
			const p256_int *digest, const p256_int *r,
			const p256_int *s);

#endif  /* ! __EC_CHIP_G_DCRYPTO_DCRYPTO_H */
