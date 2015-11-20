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

struct HASH_CTX {
	const struct HASH_VTAB *vtab;
	union {
		uint8_t buf[64];
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
