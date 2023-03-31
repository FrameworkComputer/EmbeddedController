/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Helpers for the boringssl AEC GCM interface. */

#ifndef __CROS_EC_AES_GCM_HELPERS_H
#define __CROS_EC_AES_GCM_HELPERS_H

#include "openssl/aes.h"
#include "string.h"

/* These must be included after the "openssl/aes.h" */
#include "crypto/fipsmodule/aes/internal.h"
#include "crypto/fipsmodule/modes/internal.h"

/* CRYPTO_gcm128_init initialises |ctx| to use |block| (typically AES) with
 * the given key. |block_is_hwaes| is one if |block| is |aes_hw_encrypt|.
 *
 * This API was removed in upstream:
 * https://boringssl-review.googlesource.com/c/boringssl/+/32004
 *
 * Note: The content of GCM128_CONTEXT must be initialized by this function.
 * Passing the context that remain uninitialized parts into the other
 * CRYPTO_gcm128_ functions will result undefined behavior.
 */
static inline void CRYPTO_gcm128_init(GCM128_CONTEXT *ctx, const AES_KEY *key,
				      block128_f block, int block_is_hwaes)
{
	memset(ctx, 0, sizeof(*ctx));
	CRYPTO_gcm128_init_key(&ctx->gcm_key, key, block, block_is_hwaes);
}

#endif /* __CROS_EC_AES_GCM_HELPERS_H */
