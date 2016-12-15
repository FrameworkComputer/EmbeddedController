/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * Crypto wrapper library for the g chip.
 */
#ifndef __EC_CHIP_G_DCRYPTO_DCRYPTO_H
#define __EC_CHIP_G_DCRYPTO_DCRYPTO_H

#ifdef CR50_DEV
#define CRYPTO_TEST_SETUP
#endif

#include "internal.h"

#include <stddef.h>

#include "cryptoc/hmac.h"

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

enum hashing_mode {
	HASH_SHA1 = 0,
	HASH_SHA256 = 1,
	HASH_SHA384 = 2,  /* Only supported for PKCS#1 signing */
	HASH_SHA512 = 3,  /* Only supported for PKCS#1 signing */
	HASH_NULL = 4  /* Only supported for PKCS#1 signing */
};

/*
 * AES implementation, based on a hardware AES block.
 */
int DCRYPTO_aes_init(const uint8_t *key, uint32_t key_len, const uint8_t *iv,
		enum cipher_mode c_mode, enum encrypt_mode e_mode);
int DCRYPTO_aes_block(const uint8_t *in, uint8_t *out);

void DCRYPTO_aes_write_iv(const uint8_t *iv);
void DCRYPTO_aes_read_iv(uint8_t *iv);
int DCRYPTO_aes_ctr(uint8_t *out, const uint8_t *key, uint32_t key_bits,
		const uint8_t *iv, const uint8_t *in, size_t in_len);

/*
 * SHA implementation.  This abstraction is backed by either a
 * software or hardware implementation.
 *
 * There could be only a single hardware SHA context in progress. The init
 * functions will try using the HW context, if available, unless 'sw_required'
 * is TRUE, in which case there will be no attempt to use the hardware for
 * this particular hashing session.
 */
void DCRYPTO_SHA1_init(SHA_CTX *ctx, uint32_t sw_required);
void DCRYPTO_SHA256_init(LITE_SHA256_CTX *ctx, uint32_t sw_required);
void DCRYPTO_SHA384_init(LITE_SHA384_CTX *ctx);
void DCRYPTO_SHA512_init(LITE_SHA512_CTX *ctx);
const uint8_t *DCRYPTO_SHA1_hash(const void *data, uint32_t n,
				uint8_t *digest);
const uint8_t *DCRYPTO_SHA256_hash(const void *data, uint32_t n,
				   uint8_t *digest);
const uint8_t *DCRYPTO_SHA384_hash(const void *data, uint32_t n,
				   uint8_t *digest);
const uint8_t *DCRYPTO_SHA512_hash(const void *data, uint32_t n,
				   uint8_t *digest);

/*
 *  HMAC.
 */
void DCRYPTO_HMAC_SHA256_init(LITE_HMAC_CTX *ctx, const void *key,
			unsigned int len);
const uint8_t *DCRYPTO_HMAC_final(LITE_HMAC_CTX *ctx);

/*
 * BIGNUM utility methods.
 */
void DCRYPTO_bn_wrap(struct LITE_BIGNUM *b, void *buf, size_t len);

/*
 *  RSA.
 */

/* Largest supported key size, 2048-bits. */
#define RSA_MAX_BYTES   256
#define RSA_MAX_WORDS   (RSA_MAX_BYTES / sizeof(uint32_t))
#define RSA_F4          65537

struct RSA {
	uint32_t e;
	struct LITE_BIGNUM N;
	struct LITE_BIGNUM d;
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
int DCRYPTO_rsa_verify(const struct RSA *rsa, const uint8_t *digest,
		uint32_t digest_len, const uint8_t *sig,
		const uint32_t sig_len,	enum padding_mode padding,
		enum hashing_mode hashing);

/* Calculate n = p * q, d = e ^ -1 mod phi. */
int DCRYPTO_rsa_key_compute(struct LITE_BIGNUM *N, struct LITE_BIGNUM *d,
			struct LITE_BIGNUM *p, struct LITE_BIGNUM *q,
			uint32_t e);

/*
 *  EC.
 */
int DCRYPTO_p256_base_point_mul(p256_int *out_x, p256_int *out_y,
				const p256_int *n);
int DCRYPTO_p256_point_mul(p256_int *out_x, p256_int *out_y,
			const p256_int *n, const p256_int *in_x,
			const p256_int *in_y);
int DCRYPTO_p256_key_from_bytes(p256_int *x, p256_int *y, p256_int *d,
				const uint8_t key_bytes[P256_NBYTES]);
/* P256 based integration encryption (DH+AES128+SHA256). */
/* Authenticated data may be provided, where the first auth_data_len
 * bytes of in will be authenticated but not encrypted. */
/* Supports in-place encryption / decryption. */
size_t DCRYPTO_ecies_encrypt(
	void *out, size_t out_len, const void *in, size_t in_len,
	size_t auth_data_len, const uint8_t *iv,
	const p256_int *pub_x, const p256_int *pub_y,
	const uint8_t *salt, size_t salt_len,
	const uint8_t *info, size_t info_len);
size_t DCRYPTO_ecies_decrypt(
	void *out, size_t out_len, const void *in, size_t in_len,
	size_t auth_data_len, const uint8_t *iv,
	const p256_int *d,
	const uint8_t *salt, size_t salt_len,
	const uint8_t *info, size_t info_len);

/*
 *  HKDF.
 */
int DCRYPTO_hkdf(uint8_t *OKM, size_t OKM_len,
		const uint8_t *salt, size_t salt_len,
		const uint8_t *IKM, size_t IKM_len,
		const uint8_t *info, size_t info_len);

/*
 *  BN.
 */
int DCRYPTO_bn_generate_prime(struct LITE_BIGNUM *p);
void DCRYPTO_bn_wrap(struct LITE_BIGNUM *b, void *buf, size_t len);
void DCRYPTO_bn_mul(struct LITE_BIGNUM *c, const struct LITE_BIGNUM *a,
		const struct LITE_BIGNUM *b);
int DCRYPTO_bn_div(struct LITE_BIGNUM *quotient, struct LITE_BIGNUM *remainder,
		const struct LITE_BIGNUM *input,
		const struct LITE_BIGNUM *divisor);

/*
 *  X509.
 */
int DCRYPTO_x509_verify(const uint8_t *cert, size_t len,
			const struct RSA *ca_pub_key);

/*
 * Memory related functions.
 */
int DCRYPTO_equals(const void *a, const void *b, size_t len);

#endif  /* ! __EC_CHIP_G_DCRYPTO_DCRYPTO_H */
