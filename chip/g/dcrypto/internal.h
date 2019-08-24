/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __EC_CHIP_G_DCRYPTO_INTERNAL_H
#define __EC_CHIP_G_DCRYPTO_INTERNAL_H

#include <stddef.h>
#include <string.h>

#include "common.h"
#include "util.h"

#include "cryptoc/p256.h"
#include "cryptoc/sha.h"
#include "cryptoc/sha256.h"
#include "cryptoc/sha384.h"
#include "cryptoc/sha512.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * SHA.
 */
#define CTRL_CTR_BIG_ENDIAN (__BYTE_ORDER__  == __ORDER_BIG_ENDIAN__)
#define CTRL_ENABLE         1
#define CTRL_ENCRYPT        1
#define CTRL_NO_SOFT_RESET  0

#define SHA_DIGEST_WORDS   (SHA_DIGEST_SIZE / sizeof(uint32_t))
#define SHA256_DIGEST_WORDS (SHA256_DIGEST_SIZE / sizeof(uint32_t))

#ifdef SHA512_SUPPORT
#define SHA_DIGEST_MAX_BYTES SHA512_DIGEST_SIZE
#else
#define SHA_DIGEST_MAX_BYTES SHA256_DIGEST_SIZE
#endif

enum sha_mode {
	SHA1_MODE = 0,
	SHA256_MODE = 1
};

/*
 * Use this structure to avoid alignment problems with input and output
 * pointers.
 */
struct access_helper {
	uint32_t udata;
} __packed;

#ifndef SECTION_IS_RO
int dcrypto_grab_sha_hw(void);
void dcrypto_release_sha_hw(void);
#endif
void dcrypto_sha_hash(enum sha_mode mode, const uint8_t *data,
		uint32_t n, uint8_t *digest);
void dcrypto_sha_init(enum sha_mode mode);
void dcrypto_sha_update(struct HASH_CTX *unused,
			const void *data, uint32_t n);
void dcrypto_sha_wait(enum sha_mode mode, uint32_t *digest);

/*
 * BIGNUM.
 */
#define LITE_BN_BITS2        32
#define LITE_BN_BYTES        4

struct LITE_BIGNUM {
	uint32_t dmax;              /* Size of d, in 32-bit words. */
	struct access_helper *d;  /* Word array, little endian format ... */
};

#define BN_DIGIT(b, i) ((b)->d[(i)].udata)

void bn_init(struct LITE_BIGNUM *bn, void *buf, size_t len);
#define bn_size(b) ((b)->dmax * LITE_BN_BYTES)
#define bn_words(b) ((b)->dmax)
#define bn_bits(b) ((b)->dmax * LITE_BN_BITS2)
int bn_eq(const struct LITE_BIGNUM *a, const struct LITE_BIGNUM *b);
int bn_check_topbit(const struct LITE_BIGNUM *N);
int bn_modexp(struct LITE_BIGNUM *output,
			const struct LITE_BIGNUM *input,
			const struct LITE_BIGNUM *exp,
			const struct LITE_BIGNUM *N);
int bn_modexp_word(struct LITE_BIGNUM *output,
			const struct LITE_BIGNUM *input,
			uint32_t pubexp,
			const struct LITE_BIGNUM *N);
int bn_modexp_blinded(struct LITE_BIGNUM *output,
			const struct LITE_BIGNUM *input,
			const struct LITE_BIGNUM *exp,
			const struct LITE_BIGNUM *N,
			uint32_t pubexp);
uint32_t bn_add(struct LITE_BIGNUM *c,
		const struct LITE_BIGNUM *a);
uint32_t bn_sub(struct LITE_BIGNUM *c,
		const struct LITE_BIGNUM *a);
int bn_modinv_vartime(struct LITE_BIGNUM *r,
			const struct LITE_BIGNUM *e,
			const struct LITE_BIGNUM *MOD);
int bn_is_bit_set(const struct LITE_BIGNUM *a, int n);

/*
 * Accelerated bn.
 */
int dcrypto_modexp(struct LITE_BIGNUM *output,
			const struct LITE_BIGNUM *input,
			const struct LITE_BIGNUM *exp,
			const struct LITE_BIGNUM *N);
int dcrypto_modexp_word(struct LITE_BIGNUM *output,
			const struct LITE_BIGNUM *input,
			uint32_t pubexp,
			const struct LITE_BIGNUM *N);
int dcrypto_modexp_blinded(struct LITE_BIGNUM *output,
			const struct LITE_BIGNUM *input,
			const struct LITE_BIGNUM *exp,
			const struct LITE_BIGNUM *N,
			uint32_t pubexp);

struct drbg_ctx {
	uint32_t k[SHA256_DIGEST_WORDS];
	uint32_t v[SHA256_DIGEST_WORDS];
	uint32_t reseed_counter;
};

/*
 * NIST SP 800-90A HMAC DRBG.
 */

/* Standard initialization. */
void hmac_drbg_init(struct drbg_ctx *ctx,
		    const void *p0, size_t p0_len,
		    const void *p1, size_t p1_len,
		    const void *p2, size_t p2_len);
/* Initialize for use as RFC6979 DRBG. */
void hmac_drbg_init_rfc6979(struct drbg_ctx *ctx,
			    const p256_int *key,
			    const p256_int *message);
/* Initialize with at least nbits of random entropy. */
void hmac_drbg_init_rand(struct drbg_ctx *ctx, size_t nbits);
void hmac_drbg_reseed(struct drbg_ctx *ctx,
		      const void *p0, size_t p0_len,
		      const void *p1, size_t p1_len,
		      const void *p2, size_t p2_len);
int hmac_drbg_generate(struct drbg_ctx *ctx,
		       void *out, size_t out_len,
		       const void *input, size_t input_len);
/* Generate p256, with no additional input. */
void hmac_drbg_generate_p256(struct drbg_ctx *ctx, p256_int *k_out);
void drbg_exit(struct drbg_ctx *ctx);

/*
 * Accelerated p256. FIPS PUB 186-4
 */
int dcrypto_p256_ecdsa_sign(struct drbg_ctx *drbg, const p256_int *key,
			    const p256_int *message, p256_int *r, p256_int *s)
	__attribute__((warn_unused_result));
int dcrypto_p256_base_point_mul(const p256_int *k, p256_int *x, p256_int *y)
	 __attribute__((warn_unused_result));
int dcrypto_p256_point_mul(const p256_int *k,
		const p256_int *in_x, const p256_int *in_y,
		p256_int *x, p256_int *y)
	__attribute__((warn_unused_result));
int dcrypto_p256_ecdsa_verify(const p256_int *key_x, const p256_int *key_y,
		const p256_int *message, const p256_int *r,
		const p256_int *s)
	__attribute__((warn_unused_result));
int dcrypto_p256_is_valid_point(const p256_int *x, const p256_int *y)
	__attribute__((warn_unused_result));

/*
 * Accelerator runtime.
 *
 * Note dcrypto_init_and_lock grabs a mutex and dcrypto_unlock releases it.
 * Do not use dcrypto_call, dcrypto_imem_load or dcrypto_dmem_load w/o holding
 * the mutex.
 */
void dcrypto_init_and_lock(void);
void dcrypto_unlock(void);
uint32_t dcrypto_call(uint32_t adr) __attribute__((warn_unused_result));
void dcrypto_imem_load(size_t offset, const uint32_t *opcodes,
		       size_t n_opcodes);
/*
 * Returns 0 iff no difference was observed between existing and new content.
 */
uint32_t dcrypto_dmem_load(size_t offset, const void *words, size_t n_words);

/*
 * Key ladder.
 */
#ifndef __cplusplus
enum dcrypto_appid;      /* Forward declaration. */

int dcrypto_ladder_compute_usr(enum dcrypto_appid id,
			const uint32_t usr_salt[8]);
int dcrypto_ladder_derive(enum dcrypto_appid appid, const uint32_t salt[8],
			  const uint32_t input[8], uint32_t output[8]);
#endif

#ifdef __cplusplus
}
#endif

#endif  /* ! __EC_CHIP_G_DCRYPTO_INTERNAL_H */
