/* Copyright 2012 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* SHA-256 functions */

#ifndef __CROS_EC_SHA256_H
#define __CROS_EC_SHA256_H

#include "common.h"

#define SHA256_DIGEST_SIZE 32
#define SHA256_BLOCK_SIZE 64

#ifdef __cplusplus
extern "C" {
#endif

#ifdef CONFIG_PLATFORM_EC_SHA256_HW_ZEPHYR
/*
 * The chip's header file must implement the SHA256 context structure and
 * specific functions for its hardware accelerator module.
 */
#include "sha256_hw.h"
#else
#ifdef CONFIG_SHA256_HW_ACCELERATE
/*
 * The chip's header file must implement the SHA256 context structure and
 * specific functions for its hardware accelerator module.
 */
#include "sha256_chip.h"
#else
/* SHA256 context */
struct sha256_ctx {
	uint32_t h[8];
	uint32_t tot_len;
	uint32_t len;
	uint8_t block[2 * SHA256_BLOCK_SIZE];
	uint8_t buf[SHA256_DIGEST_SIZE]; /* Used to store the final digest. */
};
#endif
#endif

void SHA256_init(struct sha256_ctx *ctx);
void SHA256_update(struct sha256_ctx *ctx, const uint8_t *data, uint32_t len);
uint8_t *SHA256_final(struct sha256_ctx *ctx);

void hmac_SHA256(uint8_t *output, const uint8_t *key, const int key_len,
		 const uint8_t *message, const int message_len);

#ifdef __cplusplus
}
#endif

#endif /* __CROS_EC_SHA256_H */
