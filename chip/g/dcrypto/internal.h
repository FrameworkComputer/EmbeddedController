/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __EC_CHIP_G_DCRYPTO_INTERNAL_H
#define __EC_CHIP_G_DCRYPTO_INTERNAL_H

#include <stdint.h>

#include "common.h"
#include "sha1.h"
#include "sha256.h"

/*
 * SHA.
 */
#define CTRL_CTR_BIG_ENDIAN (__BYTE_ORDER__  == __ORDER_BIG_ENDIAN__)
#define CTRL_ENABLE         1
#define CTRL_ENCRYPT        1
#define CTRL_NO_SOFT_RESET  0

struct HASH_CTX;      /* Forward declaration. */

struct HASH_VTAB {
	void (* const update)(struct HASH_CTX *, const uint8_t *, uint32_t);
	const uint8_t *(* const final)(struct HASH_CTX *);
	const uint8_t *(* const hash)(const uint8_t *, uint32_t, uint8_t *);
	uint32_t size;
};

#define SHA1_DIGEST_BYTES    20
#define SHA256_DIGEST_BYTES  32
#define SHA384_DIGEST_BYTES  48
#define SHA512_DIGEST_BYTES  64

#define SHA1_DIGEST_WORDS   (SHA1_DIGEST_BYTES / sizeof(uint32_t))
#define SHA256_DIGEST_WORDS (SHA256_DIGEST_BYTES / sizeof(uint32_t))
#define SHA384_DIGEST_WORDS (SHA384_DIGEST_BYTES / sizeof(uint32_t))
#define SHA512_DIGEST_WORDS (SHA512_DIGEST_BYTES / sizeof(uint32_t))

#if defined(CONFIG_SHA512)
#define SHA_DIGEST_MAX_BYTES SHA512_DIGEST_BYTES
#elif defined(CONFIG_SHA384)
#define SHA_DIGEST_MAX_BYTES SHA384_DIGEST_BYTES
#elif defined(CONFIG_SHA256)
#define SHA_DIGEST_MAX_BYTES SHA256_DIGEST_BYTES
#elif defined CONFIG_SHA1
#define SHA_DIGEST_MAX_BYTES SHA1_DIGEST_BYTES
#endif

struct HASH_CTX {
	const struct HASH_VTAB *vtab;
	union {
		uint8_t buf[SHA_DIGEST_MAX_BYTES];
		struct sha1_ctx sw_sha1;
		struct sha256_ctx sw_sha256;
	} u;
};

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
			const uint8_t *data, uint32_t n);
void dcrypto_sha_wait(enum sha_mode mode, uint32_t *digest);

/*
 * HMAC.
 */
struct HMAC_CTX {
	struct HASH_CTX hash;
	uint8_t opad[64];
};

#define HASH_update(ctx, data, len) \
	((ctx)->vtab->update((ctx), (data), (len)))
void dcrypto_HMAC_SHA256_init(struct HMAC_CTX *ctx, const void *key,
			unsigned int len);
#define dcrypto_HMAC_update(ctx, data, len) HASH_update(&(ctx)->hash, data, len)
const uint8_t *dcrypto_HMAC_final(struct HMAC_CTX *ctx);

/*
 * BIGNUM.
 */
#define BN_BITS2        32
#define BN_BYTES        4

struct BIGNUM {
	uint32_t dmax;              /* Size of d, in 32-bit words. */
	struct access_helper *d;  /* Word array, little endian format ... */
};

#define BN_DIGIT(b, i) ((b)->d[(i)].udata)

void bn_init(struct BIGNUM *bn, void *buf, size_t len);
#define bn_size(b) ((b)->dmax * BN_BYTES)
int bn_check_topbit(const struct BIGNUM *N);
void bn_mont_modexp(struct BIGNUM *output, const struct BIGNUM *input,
		const struct BIGNUM *exp, const struct BIGNUM *N);
uint32_t bn_add(struct BIGNUM *c, const struct BIGNUM *a);
uint32_t bn_sub(struct BIGNUM *c, const struct BIGNUM *a);
void bn_mul(struct BIGNUM *c, const struct BIGNUM *a, const struct BIGNUM *b);
int bn_modinv_vartime(struct BIGNUM *r, const struct BIGNUM *e,
		const struct BIGNUM *MOD);

/*
 * EC.
 */
#define P256_BITSPERDIGIT 32
#define P256_NDIGITS 8
#define P256_NBYTES 32

typedef uint32_t p256_digit;
typedef int32_t p256_sdigit;
typedef uint64_t p256_ddigit;
typedef int64_t p256_sddigit;

/* Define p256_int as a struct to leverage struct assignment. */
typedef struct {
	p256_digit a[P256_NDIGITS] __packed;
} p256_int;

#define P256_DIGITS(x) ((x)->a)
#define P256_DIGIT(x, y) ((x)->a[y])

#define P256_ZERO { {0} }
#define P256_ONE { {1} }

/* Curve constants. */
extern const p256_int SECP256r1_n;
extern const p256_int SECP256r1_p;
extern const p256_int SECP256r1_b;

void p256_init(p256_int *a);
void p256_from_bin(const uint8_t src[P256_NBYTES], p256_int *dst);
void p256_to_bin(const p256_int *src, uint8_t dst[P256_NBYTES]);
#define p256_clear(a) p256_init((a))
int p256_is_zero(const p256_int *a);
int p256_cmp(const p256_int *a, const p256_int *b);
int p256_get_bit(const p256_int *scalar, int bit);
p256_digit p256_shl(const p256_int *a, int n, p256_int *b);
void p256_shr(const p256_int *a, int n, p256_int *b);
int p256_add(const p256_int *a, const p256_int *b, p256_int *c);
int p256_add_d(const p256_int *a, p256_digit d, p256_int *b);
void p256_points_mul_vartime(
	const p256_int *n1, const p256_int *n2, const p256_int *in_x,
	const p256_int *in_y, p256_int *out_x, p256_int *out_y);
void p256_mod(const p256_int *MOD, const p256_int *in, p256_int *out);
void p256_modmul(const p256_int *MOD, const p256_int *a,
		const p256_digit top_b,	const p256_int *b, p256_int *c);
void p256_modinv(const p256_int *MOD, const p256_int *a, p256_int *b);
void p256_modinv_vartime(const p256_int *MOD, const p256_int *a, p256_int *b);

/*
 * Utility functions.
 */
/* TODO(ngm): memset that doesn't get optimized out. */
#define dcrypto_memset(p, b, len)  memset((p), (b), (len))

#endif  /* ! __EC_CHIP_G_DCRYPTO_INTERNAL_H */
