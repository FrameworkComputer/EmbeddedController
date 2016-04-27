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

/*
 * SHA.
 */
#define CTRL_CTR_BIG_ENDIAN (__BYTE_ORDER__  == __ORDER_BIG_ENDIAN__)
#define CTRL_ENABLE         1
#define CTRL_ENCRYPT        1
#define CTRL_NO_SOFT_RESET  0

#define SHA_DIGEST_WORDS   (SHA_DIGEST_SIZE / sizeof(uint32_t))
#define SHA256_DIGEST_WORDS (SHA256_DIGEST_SIZE / sizeof(uint32_t))

#define SHA_DIGEST_MAX_BYTES SHA256_DIGEST_SIZE

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
#define BN_BITS2        32
#define BN_BYTES        4

struct BIGNUM {
	uint32_t dmax;              /* Size of d, in 32-bit words. */
	struct access_helper *d;  /* Word array, little endian format ... */
};

#define BN_DIGIT(b, i) ((b)->d[(i)].udata)

void bn_init(struct BIGNUM *bn, void *buf, size_t len);
#define bn_size(b) ((b)->dmax * BN_BYTES)
#define bn_bits(b) ((b)->dmax * BN_BITS2)
int bn_check_topbit(const struct BIGNUM *N);
void bn_mont_modexp(struct BIGNUM *output, const struct BIGNUM *input,
		const struct BIGNUM *exp, const struct BIGNUM *N);
uint32_t bn_add(struct BIGNUM *c, const struct BIGNUM *a);
uint32_t bn_sub(struct BIGNUM *c, const struct BIGNUM *a);
void bn_mul(struct BIGNUM *c, const struct BIGNUM *a, const struct BIGNUM *b);
int bn_modinv_vartime(struct BIGNUM *r, const struct BIGNUM *e,
		const struct BIGNUM *MOD);

/*
 * Utility functions.
 */
/* TODO(ngm): memset that doesn't get optimized out. */
#define dcrypto_memset(p, b, len)  memset((p), (b), (len))

#endif  /* ! __EC_CHIP_G_DCRYPTO_INTERNAL_H */
