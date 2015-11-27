/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __EC_CHIP_G_DCRYPTO_INTERNAL_H
#define __EC_CHIP_G_DCRYPTO_INTERNAL_H

#include <inttypes.h>

#include "common.h"
#include "sha1.h"
#include "sha256.h"

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

#endif  /* ! __EC_CHIP_G_DCRYPTO_INTERNAL_H */
