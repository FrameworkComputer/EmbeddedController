/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "dcrypto.h"
#include "internal.h"
#include "registers.h"

#include "cryptoc/sha.h"

static void dcrypto_sha1_init(SHA_CTX *ctx);
static const uint8_t *dcrypto_sha1_final(SHA_CTX *unused);

/*
 * Hardware SHA implementation.
 */
static const HASH_VTAB HW_SHA1_VTAB = {
	dcrypto_sha1_init,
	dcrypto_sha_update,
	dcrypto_sha1_final,
	DCRYPTO_SHA1_hash,
	SHA_DIGEST_SIZE
};

/* Requires dcrypto_grab_sha_hw() to be called first. */
static void dcrypto_sha1_init(SHA_CTX *ctx)
{
	ctx->f = &HW_SHA1_VTAB;
	dcrypto_sha_init(SHA1_MODE);
}

/* Select and initialize either the software or hardware
 * implementation.  If "multi-threaded" behaviour is required, then
 * callers must set sw_required to 1.  This is because SHA1 state
 * internal to the hardware cannot be extracted, so it is not possible
 * to suspend and resume a hardware based SHA operation.
 *
 * If the caller has no preference as to implementation, then hardware
 * is preferred based on availability.  Hardware is considered to be
 * in use between init() and finished() calls. */
void DCRYPTO_SHA1_init(SHA_CTX *ctx, uint32_t sw_required)
{
	if (!sw_required && dcrypto_grab_sha_hw())
		dcrypto_sha1_init(ctx);
	else
		SHA_init(ctx);
}

static const uint8_t *dcrypto_sha1_final(SHA_CTX *ctx)
{
	dcrypto_sha_wait(SHA1_MODE, (uint32_t *) ctx->buf);
	return ctx->buf;
}

const uint8_t *DCRYPTO_SHA1_hash(const void *data, uint32_t n,
				uint8_t *digest)
{
	if (dcrypto_grab_sha_hw())
		/* dcrypto_sha_wait() will release the hw. */
		dcrypto_sha_hash(SHA1_MODE, data, n, digest);
	else
		SHA_hash(data, n, digest);
	return digest;
}
