/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "dcrypto.h"
#include "debug_printf.h"
#include "registers.h"
#include "setup.h"
#include "trng.h"

#define LOADERKEYEXP 3
#define RSA_NUM_WORDS 96
#define RSA_NUM_BYTES (RSA_NUM_WORDS * 4)

#define RANDOM_STEP 5

inline uint32_t bswap(uint32_t a)
{
	uint32_t result;

	__asm__ volatile("rev %0, %1;" : "=r"(result) : "r"(a));

	return result;
}

/* Montgomery c[] += a * b[] / R % key. */
static void montMulAdd(const uint32_t *key,
		       uint32_t *c, const uint32_t a,
		       const uint32_t *b)
{
	register uint64_t tmp;
	uint32_t i, A, B, d0;

	{

		tmp = c[0] + (uint64_t)a * b[0];
		A = tmp >> 32;
		d0 = (uint32_t)tmp * *key++;
		tmp = (uint32_t)tmp + (uint64_t)d0 * *key++;
		B = tmp >> 32;
	}

	for (i = 0; i < RSA_NUM_WORDS - 1; ++i) {
		tmp = A + (uint64_t)a * b[i + 1] + c[i + 1];
		A = tmp >> 32;
		tmp = B + (uint64_t)d0 * *key++ + (uint32_t)tmp;
		c[i] = (uint32_t)tmp;
		B = tmp >> 32;
	}

	c[RSA_NUM_WORDS - 1] = A + B;
}

/* Montgomery c[] = a[] * b[] / R % key. */
static void montMul(const uint32_t *key,
		    uint32_t *c, const uint32_t *a,
		    const uint32_t *b)
{
	int i;

	for (i = 0; i < RSA_NUM_WORDS; ++i)
		c[i] = 0;

	for (i = 0; i < RSA_NUM_WORDS; ++i)
		montMulAdd(key, c, a[i], b);
}

/* Montgomery c[] = a[] * 1 / R % key. */
static void montMul1(const uint32_t *key,
		     uint32_t *c, const uint32_t *a)
{
	int i;

	for (i = 0; i < RSA_NUM_WORDS; ++i)
		c[i] = 0;

	montMulAdd(key, c, 1, a);
	for (i = 1; i < RSA_NUM_WORDS; ++i)
		montMulAdd(key, c, 0, a);
}

/* In-place exponentiation to power 3 % key. */
static void modpow3(const uint32_t *key,
		    const uint32_t *signature, uint32_t *out)
{
	static uint32_t aaR[RSA_NUM_WORDS];
	static uint32_t aaaR[RSA_NUM_WORDS];

	montMul(key, aaR, signature, signature);
	montMul(key, aaaR, aaR, signature);
	montMul1(key, out, aaaR);
}

void LOADERKEY_verify(const uint32_t *key, const uint32_t *signature,
		      const uint32_t *sha256)
{
	static uint32_t buf[RSA_NUM_WORDS]
		__attribute__((section(".guarded_data")));
	static uint32_t hash[SHA256_DIGEST_WORDS]
		__attribute__((section(".guarded_data")));
	uint32_t step, offset;
	int i;

	modpow3(key, signature, buf);
	VERBOSE("sig %.384h\n", buf);

	/*
	 * If key was not 3Kb, assume 2Kb and expand for subsequent
	 * padding + hash verification mangling.
	 */
	if (key[96] == 0) {
		buf[95] ^= buf[63];
		buf[63] ^= 0x1ffff;
		for (i = 63; i < 95; ++i)
			buf[i] ^= -1;
	}

	/*
	 * XOR in offsets across buf. Mostly to get rid of all those -1 words
	 * in there.
	 */
	offset = rand() % RSA_NUM_WORDS;
	step = (RANDOM_STEP % RSA_NUM_WORDS) || 1;

	for (i = 0; i < RSA_NUM_WORDS; ++i) {
		buf[offset] ^= (0x1000u + offset);
		offset = (offset + step) % RSA_NUM_WORDS;
	}

	/*
	 * Xor digest location, so all words becomes 0 only iff equal.
	 *
	 * Also XOR in offset and non-zero const. This to avoid repeat
	 * glitches to zero be able to produce the right result.
	 */
	offset = rand() % SHA256_DIGEST_WORDS;
	step = (RANDOM_STEP % SHA256_DIGEST_WORDS) || 1;
	for (i = 0; i < SHA256_DIGEST_WORDS; ++i) {
		buf[offset] ^= bswap(sha256[SHA256_DIGEST_WORDS - 1 - offset])
			^ (offset + 0x10u);
		offset = (offset + step) % SHA256_DIGEST_WORDS;
	}

	VERBOSE("\nsig^ %.384h\n\n", buf);

	/* Hash resulting buffer. */
	DCRYPTO_SHA256_hash((uint8_t *) buf, RSA_NUM_BYTES, (uint8_t *) hash);

	VERBOSE("hash %.32h\n", hash);

	/*
	 * Write computed hash to unlock register to unlock execution, iff
	 * right. Idea is that this flow cannot be glitched to have correct
	 * values with any probability.
	 */
	for (i = 0; i < SHA256_DIGEST_WORDS; ++i)
		GREG32_ADDR(GLOBALSEC, SB_BL_SIG0)[i] = hash[i];

	/*
	 * Make an unlock attempt. Value written is irrelevant, as long as
	 * something is written.
	 */
	GREG32(GLOBALSEC, SIG_UNLOCK) = 1;
}
