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

#define SHA1_DIGEST_BYTES    20
#define SHA256_DIGEST_BYTES  32
#define SHA384_DIGEST_BYTES  48
#define SHA512_DIGEST_BYTES  64
#define SHA_DIGEST_MAX_BYTES SHA512_DIGEST_BYTES

#define SHA1_DIGEST_WORDS   (SHA1_DIGEST_BYTES / sizeof(uint32_t))
#define SHA256_DIGEST_WORDS (SHA256_DIGEST_BYTES / sizeof(uint32_t))
#define SHA384_DIGEST_WORDS (SHA384_DIGEST_BYTES / sizeof(uint32_t))
#define SHA512_DIGEST_WORDS (SHA512_DIGEST_BYTES / sizeof(uint32_t))

struct HASH_CTX;   /* Forward declaration. */

typedef struct HASH_CTX SHA1_CTX;
typedef struct HASH_CTX SHA256_CTX;

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

#endif  /* ! __EC_CHIP_G_DCRYPTO_DCRYPTO_H */
