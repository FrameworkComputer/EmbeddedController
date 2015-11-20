/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "dcrypto.h"
#include "internal.h"
#include "registers.h"

static void sw_sha1_init(SHA1_CTX *ctx);
static void sw_sha1_update(SHA1_CTX *ctx, const uint8_t *data, uint32_t len);
static const uint8_t *sw_sha1_final(SHA1_CTX *ctx);
static const uint8_t *sha1_hash(const uint8_t *data, uint32_t len,
				uint8_t *digest);
static const uint8_t *dcrypto_sha1_final(SHA1_CTX *unused);

/* Software SHA1 implementation. */
static const struct HASH_VTAB SW_SHA1_VTAB = {
	sw_sha1_update,
	sw_sha1_final,
	sha1_hash,
	SHA1_DIGEST_BYTES
};

static void sw_sha1_init(SHA1_CTX *ctx)
{
	ctx->vtab = &SW_SHA1_VTAB;
	sha1_init(&ctx->u.sw_sha1);
}

static void sw_sha1_update(SHA1_CTX *ctx, const uint8_t *data, uint32_t len)
{
	sha1_update(&ctx->u.sw_sha1, data, len);
}

static const uint8_t *sw_sha1_final(SHA1_CTX *ctx)
{
	return sha1_final(&ctx->u.sw_sha1);
}

static const uint8_t *sha1_hash(const uint8_t *data, uint32_t len,
				uint8_t *digest)
{
	SHA1_CTX ctx;

	sw_sha1_init(&ctx);
	sw_sha1_update(&ctx, data, len);
	memcpy(digest, sw_sha1_final(&ctx), SHA1_DIGEST_BYTES);
	return digest;
}


/*
 * Hardware SHA implementation.
 */
static const struct HASH_VTAB HW_SHA1_VTAB = {
	dcrypto_sha_update,
	dcrypto_sha1_final,
	DCRYPTO_SHA1_hash,
	SHA1_DIGEST_BYTES
};

/* Select and initialize either the software or hardware
 * implementation.  If "multi-threaded" behaviour is required, then
 * callers must set sw_required to 1.  This is because SHA1 state
 * internal to the hardware cannot be extracted, so it is not possible
 * to suspend and resume a hardware based SHA operation.
 *
 * If the caller has no preference as to implementation, then hardware
 * is preferred based on availability.  Hardware is considered to be
 * in use between init() and finished() calls. */
void DCRYPTO_SHA1_init(SHA1_CTX *ctx, uint32_t sw_required)
{
	if (!sw_required && dcrypto_grab_sha_hw()) {
		ctx->vtab = &HW_SHA1_VTAB;
		dcrypto_sha_init(SHA1_MODE);
	} else {
		sw_sha1_init(ctx);
	}
}

static const uint8_t *dcrypto_sha1_final(SHA1_CTX *ctx)
{
	dcrypto_sha_wait(SHA1_MODE, (uint32_t *) ctx->u.buf);
	return ctx->u.buf;
}

const uint8_t *DCRYPTO_SHA1_hash(const uint8_t *data, uint32_t n,
				uint8_t *digest)
{
	if (dcrypto_grab_sha_hw())
		/* dcrypto_sha_wait() will release the hw. */
		dcrypto_sha_hash(SHA1_MODE, data, n, digest);
	else
		sha1_hash(data, n, digest);
	return digest;
}

