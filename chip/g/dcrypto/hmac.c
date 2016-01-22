/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <stdint.h>

#include "internal.h"
#include "dcrypto.h"

static void HMAC_init(struct HMAC_CTX *ctx, const void *key, unsigned int len)
{
	unsigned int i;

	memset(&ctx->opad[0], 0, sizeof(ctx->opad));

	if (len > sizeof(ctx->opad)) {
		DCRYPTO_SHA256_init(&ctx->hash, 0);
		DCRYPTO_HASH_update(&ctx->hash, key, len);
		memcpy(&ctx->opad[0], DCRYPTO_HASH_final(&ctx->hash),
			DCRYPTO_HASH_size(&ctx->hash));
	} else {
		memcpy(&ctx->opad[0], key, len);
	}

	for (i = 0; i < sizeof(ctx->opad); ++i)
		ctx->opad[i] ^= 0x36;

	DCRYPTO_SHA256_init(&ctx->hash, 0);
	/* hash ipad */
	DCRYPTO_HASH_update(&ctx->hash, ctx->opad, sizeof(ctx->opad));

	for (i = 0; i < sizeof(ctx->opad); ++i)
		ctx->opad[i] ^= (0x36 ^ 0x5c);
}

void dcrypto_HMAC_SHA256_init(struct HMAC_CTX *ctx, const void *key,
			unsigned int len)
{
	DCRYPTO_SHA256_init(&ctx->hash, 0);
	HMAC_init(ctx, key, len);
}

const uint8_t *dcrypto_HMAC_final(struct HMAC_CTX *ctx)
{
	uint8_t digest[SHA_DIGEST_MAX_BYTES];  /* upto SHA2 */

	memcpy(digest, DCRYPTO_HASH_final(&ctx->hash),
		(DCRYPTO_HASH_size(&ctx->hash) <= sizeof(digest) ?
			DCRYPTO_HASH_size(&ctx->hash) : sizeof(digest)));
	DCRYPTO_SHA256_init(&ctx->hash, 0);
	DCRYPTO_HASH_update(&ctx->hash, ctx->opad, sizeof(ctx->opad));
	DCRYPTO_HASH_update(&ctx->hash, digest, DCRYPTO_HASH_size(&ctx->hash));
	memset(&ctx->opad[0], 0, sizeof(ctx->opad));  /* wipe key */
	return DCRYPTO_HASH_final(&ctx->hash);
}
