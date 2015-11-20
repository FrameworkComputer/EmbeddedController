/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __EC_CHIP_G_LOADER_HW_SHA256_H
#define __EC_CHIP_G_LOADER_HW_SHA256_H

#include <inttypes.h>
#include <stddef.h>

#define SHA256_DIGEST_BYTES 32
#define SHA256_DIGEST_WORDS (SHA256_DIGEST_BYTES / sizeof(uint32_t))

typedef struct {
	uint32_t digest[SHA256_DIGEST_WORDS];
} hwSHA256_CTX;

void hwSHA256_init(hwSHA256_CTX *ctx);
void hwSHA256_update(hwSHA256_CTX *ctx, const void *data, size_t len);
const uint8_t *hwSHA256_final(hwSHA256_CTX *ctx);

void hwSHA256(const void *data, size_t len, uint32_t *digest);

#endif  /* __EC_CHIP_G_LOADER_HW_SHA256_H */
