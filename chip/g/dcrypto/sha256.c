/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "dcrypto.h"
#include "internal.h"
#include "registers.h"
#include "util.h"

#include "cryptoc/sha256.h"

static void dcrypto_sha256_init(LITE_SHA256_CTX *ctx);
static const uint8_t *dcrypto_sha256_final(LITE_SHA256_CTX *ctx);

#ifdef SECTION_IS_RO
/* RO is single threaded. */
#define mutex_lock(x)
#define mutex_unlock(x)
static inline int dcrypto_grab_sha_hw(void)
{
	return 1;
}
static inline void dcrypto_release_sha_hw(void)
{
}
#else
#include "task.h"
static struct mutex hw_busy_mutex;

static int hw_busy;

int dcrypto_grab_sha_hw(void)
{
	int rv = 0;

	mutex_lock(&hw_busy_mutex);
	if (!hw_busy) {
		rv = 1;
		hw_busy = 1;
	}
	mutex_unlock(&hw_busy_mutex);

	return rv;
}

void dcrypto_release_sha_hw(void)
{
	mutex_lock(&hw_busy_mutex);
	hw_busy = 0;
	mutex_unlock(&hw_busy_mutex);
}

#endif  /* ! SECTION_IS_RO */

void dcrypto_sha_wait(enum sha_mode mode, uint32_t *digest)
{
	int i;
	const int digest_len = (mode == SHA1_MODE) ?
		SHA_DIGEST_SIZE :
		SHA256_DIGEST_SIZE;

	/* Stop LIVESTREAM mode. */
	GREG32(KEYMGR, SHA_TRIG) = GC_KEYMGR_SHA_TRIG_TRIG_STOP_MASK;

	/* Wait for SHA DONE interrupt. */
	while (!GREG32(KEYMGR, SHA_ITOP))
		;

	/* Read out final digest. */
	for (i = 0; i < digest_len / 4; ++i)
		*digest++ = GR_KEYMGR_SHA_HASH(i);
	dcrypto_release_sha_hw();
}

/* Hardware SHA implementation. */
static const HASH_VTAB HW_SHA256_VTAB = {
	dcrypto_sha256_init,
	dcrypto_sha_update,
	dcrypto_sha256_final,
	DCRYPTO_SHA256_hash,
	SHA256_DIGEST_SIZE
};

void dcrypto_sha_hash(enum sha_mode mode, const uint8_t *data, uint32_t n,
		uint8_t *digest)
{
	dcrypto_sha_init(mode);
	dcrypto_sha_update(NULL, data, n);
	dcrypto_sha_wait(mode, (uint32_t *) digest);
}

void dcrypto_sha_update(struct HASH_CTX *unused,
			const void *data, uint32_t n)
{
	const uint8_t *bp = (const uint8_t *) data;
	const uint32_t *wp;

	/* Feed unaligned start bytes. */
	while (n != 0 && ((uint32_t)bp & 3)) {
		GREG8(KEYMGR, SHA_INPUT_FIFO) = *bp++;
		n -= 1;
	}

	/* Feed groups of aligned words. */
	wp = (uint32_t *)bp;
	while (n >= 8*4) {
		GREG32(KEYMGR, SHA_INPUT_FIFO) = *wp++;
		GREG32(KEYMGR, SHA_INPUT_FIFO) = *wp++;
		GREG32(KEYMGR, SHA_INPUT_FIFO) = *wp++;
		GREG32(KEYMGR, SHA_INPUT_FIFO) = *wp++;
		GREG32(KEYMGR, SHA_INPUT_FIFO) = *wp++;
		GREG32(KEYMGR, SHA_INPUT_FIFO) = *wp++;
		GREG32(KEYMGR, SHA_INPUT_FIFO) = *wp++;
		GREG32(KEYMGR, SHA_INPUT_FIFO) = *wp++;
		n -= 8*4;
	}
	/* Feed individual aligned words. */
	while (n >= 4) {
		GREG32(KEYMGR, SHA_INPUT_FIFO) = *wp++;
		n -= 4;
	}

	/* Feed remaing bytes. */
	bp = (uint8_t *) wp;
	while (n != 0) {
		GREG8(KEYMGR, SHA_INPUT_FIFO) = *bp++;
		n -= 1;
	}
}

void dcrypto_sha_init(enum sha_mode mode)
{
	int val;

	/* Stop LIVESTREAM mode, in case final() was not called. */
	GREG32(KEYMGR, SHA_TRIG) = GC_KEYMGR_SHA_TRIG_TRIG_STOP_MASK;
	/* Clear interrupt status. */
	GREG32(KEYMGR, SHA_ITOP) = 0;

	/* Enable streaming mode. */
	val = GC_KEYMGR_SHA_CFG_EN_LIVESTREAM_MASK;
	/* Enable SHA DONE interrupt. */
	val |= GC_KEYMGR_SHA_CFG_EN_INT_EN_DONE_MASK;
	/* Select SHA mode. */
	if (mode == SHA1_MODE)
		val |= GC_KEYMGR_SHA_CFG_EN_SHA1_MASK;
	GREG32(KEYMGR, SHA_CFG_EN) = val;

	/* Start SHA engine. */
	GREG32(KEYMGR, SHA_TRIG) = GC_KEYMGR_SHA_TRIG_TRIG_GO_MASK;
}

static void dcrypto_sha256_init(LITE_SHA256_CTX *ctx)
{
	ctx->f = &HW_SHA256_VTAB;
	dcrypto_sha_init(SHA256_MODE);
}

/* Requires dcrypto_grab_sha_hw() to be called first. */
void DCRYPTO_SHA256_init(LITE_SHA256_CTX *ctx, uint32_t sw_required)
{
	if (!sw_required && dcrypto_grab_sha_hw())
		dcrypto_sha256_init(ctx);
#ifndef SECTION_IS_RO
	else
		SHA256_init(ctx);
#endif
}

static const uint8_t *dcrypto_sha256_final(LITE_SHA256_CTX *ctx)
{
	dcrypto_sha_wait(SHA256_MODE, (uint32_t *) ctx->buf);
	return ctx->buf;
}

const uint8_t *DCRYPTO_SHA256_hash(const void *data, uint32_t n,
				uint8_t *digest)
{
	if (dcrypto_grab_sha_hw())
		/* dcrypto_sha_wait() will release the hw. */
		dcrypto_sha_hash(SHA256_MODE, data, n, digest);
#ifndef SECTION_IS_RO
	else
		SHA256_hash(data, n, digest);
#endif
	return digest;
}
