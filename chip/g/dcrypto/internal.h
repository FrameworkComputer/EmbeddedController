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
int bn_check_topbit(const struct LITE_BIGNUM *N);
void bn_mont_modexp(struct LITE_BIGNUM *output, const struct LITE_BIGNUM *input,
		const struct LITE_BIGNUM *exp, const struct LITE_BIGNUM *N);
void bn_mont_modexp_asm(struct LITE_BIGNUM *output,
			const struct LITE_BIGNUM *input,
			const struct LITE_BIGNUM *exp,
			const struct LITE_BIGNUM *N);
uint32_t bn_add(struct LITE_BIGNUM *c, const struct LITE_BIGNUM *a);
uint32_t bn_sub(struct LITE_BIGNUM *c, const struct LITE_BIGNUM *a);
void bn_mul(struct LITE_BIGNUM *c, const struct LITE_BIGNUM *a,
	    const struct LITE_BIGNUM *b);
int bn_modinv_vartime(struct LITE_BIGNUM *r, const struct LITE_BIGNUM *e,
		const struct LITE_BIGNUM *MOD);
int bn_is_bit_set(const struct LITE_BIGNUM *a, int n);

/*
 * Runtime.
 */
void dcrypto_init(void);
uint32_t dcrypto_call(uint32_t adr);
void dcrypto_imem_load(size_t offset, const uint32_t *opcodes,
		       size_t n_opcodes);
void dcrypto_dmem_load(size_t offset, const void *words, size_t n_words);

/*
 * Utility functions.
 */
/* TODO(ngm): memset that doesn't get optimized out. */
#define dcrypto_memset(p, b, len)  memset((p), (b), (len))

#endif  /* ! __EC_CHIP_G_DCRYPTO_INTERNAL_H */
