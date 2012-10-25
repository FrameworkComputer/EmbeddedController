/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* SHA-256 functions */

#ifndef _CROS_EC_SHA256_H
#define _CROS_EC_SHA256_H

#include "common.h"

#define SHA256_DIGEST_SIZE 32
#define SHA256_BLOCK_SIZE 64

/* SHA256 context */
struct sha256_ctx {
	uint32_t h[8];
	uint32_t tot_len;
	uint32_t len;
	uint8_t block[2 * SHA256_BLOCK_SIZE];
	uint8_t buf[SHA256_DIGEST_SIZE];  /* Used to store the final digest. */
};

void SHA256_init(struct sha256_ctx *ctx);
void SHA256_update(struct sha256_ctx *ctx, const uint8_t *data, uint32_t len);
uint8_t *SHA256_final(struct sha256_ctx *ctx);

#endif  /* _CROS_EC_SHA256_H */
