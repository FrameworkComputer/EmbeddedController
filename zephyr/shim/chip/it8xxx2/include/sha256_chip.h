/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_IT8XXX2_SHA256_H
#define __CROS_EC_IT8XXX2_SHA256_H

#include "common.h"

struct sha256_ctx {
	/* W[0] ~ W[15] */
	uint32_t w[16];
	/* reserved */
	uint32_t reserved1[8];
	/* H[0] ~ H[7] */
	uint32_t h[8];
	/* reserved */
	uint32_t reserved2[30];
	uint32_t w_index;
	uint32_t total_len;
	/* K[0] ~ K[63] */
	uint32_t k[64];
} __aligned(256);

void SHA256_abort(struct sha256_ctx *ctx);

#endif /* __CROS_EC_IT8XXX2_SHA256_H */
