/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_SHA256_CHIP_H
#define __CROS_EC_SHA256_CHIP_H

#include "common.h"

#define NPCX_SHA256_HANDLE_SIZE    212
struct sha256_ctx {
	/* the context handle required for SHA256 API */
	uint8_t handle[NPCX_SHA256_HANDLE_SIZE];
	/*
	 * This is used to buffer:
	 *   1. the result (digest) of the SHA256 computation.
	 *   2. the 1st block input data (the key padding) for hmac_SHA256_step.
	 */
	uint8_t buf[SHA256_BLOCK_SIZE];
} __aligned(4);

void SHA256_abort(struct sha256_ctx *ctx);

#endif  /* __CROS_EC_SHA256_CHIP_H */
