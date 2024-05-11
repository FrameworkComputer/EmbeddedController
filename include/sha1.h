/* Copyright 2014 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* SHA-1 functions */

#ifndef __CROS_EC_SHA1_H
#define __CROS_EC_SHA1_H

#include "common.h"
#ifdef HOST_TOOLS_BUILD
#include <string.h>
#define DIV_ROUND_UP(x, y) (((x) + ((y)-1)) / (y))
#else
#include "util.h"

#ifdef __cplusplus
extern "C" {
#endif
#endif

#define SHA1_DIGEST_SIZE 20
#define SHA1_BLOCK_SIZE 64

/* SHA-1 context */
struct sha1_ctx {
	uint32_t count;
	uint32_t state[5];
	union {
		uint8_t b[SHA1_BLOCK_SIZE];
		uint32_t w[DIV_ROUND_UP(SHA1_BLOCK_SIZE, sizeof(uint32_t))];
	} buf;
};

void sha1_init(struct sha1_ctx *ctx);
void sha1_update(struct sha1_ctx *ctx, const uint8_t *data, uint32_t len);
uint8_t *sha1_final(struct sha1_ctx *ctx);

#ifdef __cplusplus
}
#endif

#endif /* __CROS_EC_SHA1_H */
