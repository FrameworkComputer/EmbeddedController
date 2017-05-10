/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "internal.h"
#include "dcrypto.h"

#include <stdint.h>

#include "cryptoc/sha256.h"
#include "cryptoc/util.h"

/* TODO(ngm): add support for hardware hmac. */
static void HMAC_init(LITE_HMAC_CTX *ctx, const void *key, unsigned int len)
{
	unsigned int i;

	memset(&ctx->opad[0], 0, sizeof(ctx->opad));

	if (len > sizeof(ctx->opad)) {
		DCRYPTO_SHA256_init(&ctx->hash, 0);
		HASH_update(&ctx->hash, key, len);
		memcpy(&ctx->opad[0], HASH_final(&ctx->hash),
			HASH_size(&ctx->hash));
	} else {
		memcpy(&ctx->opad[0], key, len);
	}

	for (i = 0; i < sizeof(ctx->opad); ++i)
		ctx->opad[i] ^= 0x36;

	DCRYPTO_SHA256_init(&ctx->hash, 0);
	/* hash ipad */
	HASH_update(&ctx->hash, ctx->opad, sizeof(ctx->opad));

	for (i = 0; i < sizeof(ctx->opad); ++i)
		ctx->opad[i] ^= (0x36 ^ 0x5c);
}

void DCRYPTO_HMAC_SHA256_init(LITE_HMAC_CTX *ctx, const void *key,
			unsigned int len)
{
	HMAC_init(ctx, key, len);
}

const uint8_t *DCRYPTO_HMAC_final(LITE_HMAC_CTX *ctx)
{
	uint8_t digest[SHA_DIGEST_MAX_BYTES];  /* upto SHA2 */

	memcpy(digest, HASH_final(&ctx->hash),
		(HASH_size(&ctx->hash) <= sizeof(digest) ?
			HASH_size(&ctx->hash) : sizeof(digest)));
	DCRYPTO_SHA256_init(&ctx->hash, 0);
	HASH_update(&ctx->hash, ctx->opad, sizeof(ctx->opad));
	HASH_update(&ctx->hash, digest, HASH_size(&ctx->hash));
	always_memset(&ctx->opad[0], 0, sizeof(ctx->opad));  /* wipe key */
	return HASH_final(&ctx->hash);
}
