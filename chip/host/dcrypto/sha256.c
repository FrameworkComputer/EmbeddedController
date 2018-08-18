/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "dcrypto.h"

void DCRYPTO_SHA256_init(LITE_SHA256_CTX *ctx, uint32_t sw_required)
{
	SHA256_init(ctx);
}

const uint8_t *DCRYPTO_SHA256_hash(const void *data, uint32_t n,
				uint8_t *digest)
{
	SHA256_hash(data, n, digest);
	return digest;
}
