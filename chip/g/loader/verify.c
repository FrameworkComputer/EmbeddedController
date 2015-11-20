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


static const uint32_t LOADERKEY_A[RSA_NUM_WORDS + 1] = {
	0xea54076f, 0xe986c871, 0x8cffffb4, 0xd7c50bda, 0x30700ee0, 0xc023a878,
	0x30e7fdf8, 0x5bb0c06f, 0x1d25d80f, 0x18e181f7, 0xfbf7a8b0, 0x331c16d4,
	0xeb042379, 0x4cef13ec, 0x5b2072e7, 0xc807b01d, 0x443fb117, 0xd2e04e5b,
	0xcb984393, 0x85d90d9d, 0x0332dcb8, 0xd42ccacf, 0x787e3947, 0x1975095c,
	0x2d523b0b, 0xf815be95, 0x00db9a2c, 0x6c08442b, 0x57a022bb, 0x9d5c84ed,
	0x46a6d275, 0x4392dcf8, 0xfa6812e3, 0xe0f3a3e6, 0xc8ff3f61, 0xd518dbac,
	0xbba7376a, 0x767a219a, 0x9d153119, 0x980b16f8, 0x79eb5078, 0xb869924d,
	0x2e392cc2, 0x76c04f32, 0xe35ea788, 0xcb67fa62, 0x30efec79, 0x36f04ae0,
	0x2212a5fc, 0x51c41de8, 0x2b0b84db, 0x6803ca1c, 0x39a248fd, 0xa0c31ee2,
	0xb1ca22b6, 0x16e54056, 0x086f6591, 0x3825208d, 0x079c157b, 0xe51c15a6,
	0x0dd1c66f, 0x8267b8ae, 0xf06b4f85, 0xc68b27ab, 0x31bcd5fc, 0x34d563b7,
	0xc4d2212e, 0x1e770199, 0xaf797061, 0x824d4853, 0x526e18cd, 0x4bb8a0dc,
	0xeb9377fe, 0x04fda73c, 0x2933f8a6, 0xe16c0432, 0x40ea1bd5, 0x9efcd77e,
	0x92be9e55, 0x003c1128, 0x48442cf9, 0x80b4fb31, 0xfe1e3df3, 0x1d28e14d,
	0xe99c0f9d, 0x521d38c2, 0x0082c4f1, 0xcff25d56, 0x0d3e7186, 0xe72b98f0,
	0xefaa5689, 0x74051ed5, 0x6b7e7fff, 0x822b1944, 0x77a94732, 0x8d0b9aaf,
	0x7a8ee958
};

const uint32_t LOADERKEY_B[RSA_NUM_WORDS + 1] = {
	0xeea8b39f, 0xdfa457a1, 0x8b81fdc3, 0xb0204c84, 0x297b9db2, 0xaa70318d,
	0x8cd41a68, 0x4aa0f9bb, 0xf63f9d69, 0xf0fe64b0, 0x96e42e2d, 0x5e494b1d,
	0x066cefd0, 0xde949c16, 0xc92499ed, 0x92229990, 0x48ac3b1a, 0x1dfc2388,
	0xda71d258, 0x826ddedf, 0xd0220e70, 0x6140dedf, 0x92bcdec7, 0xcdf91c22,
	0xaa110aed, 0xc371c2f9, 0xa3fedf2a, 0xfd2c6a07, 0xe71aabce, 0x7f426484,
	0x0ac51128, 0x4bab4ca2, 0x0162d0b9, 0x49fef7e3, 0xeda8664e, 0x14b92b7a,
	0x0397dbb7, 0x5b9eb94a, 0x069b5059, 0x3851f46b, 0x45bbcaba, 0x0b812652,
	0x7cd8b10b, 0xcaeccc32, 0x0ffd08e3, 0xfe6f0306, 0x8c02d5f7, 0xafdc4595,
	0xe0edda47, 0x0cc821db, 0x50beeae5, 0xb9868c18, 0xefd2de11, 0xdfecd15c,
	0xa8937a70, 0x223d9d95, 0x1b70848b, 0x54fa9176, 0x8bf012ef, 0xd37c1446,
	0xf9a7ebeb, 0xbf2dfa9a, 0xdc6b8ea0, 0xe5f8bc4d, 0x539222b5, 0x192521e4,
	0xe7088628, 0x2646bb56, 0x6fcc5d70, 0x3f1cd8e9, 0xae9cec24, 0xf53b6559,
	0x6f091891, 0x5342fa61, 0xbfee50e9, 0x211ad58a, 0xd1c5aa17, 0x252dfa56,
	0x17131164, 0x4630a459, 0x2f681f51, 0x3fb9ab3c, 0x6c8e0a70, 0xa34a868b,
	0xe960e702, 0xa470d241, 0x00647369, 0xa4c25391, 0xd1926cf9, 0x5fce5488,
	0xd171cb2e, 0x8a7c982e, 0xc89cbe39, 0xc0e019d8, 0x82cd1ebe, 0x68918fce,
	0x5ec138fd
};

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

#if LOADERKEYEXP == 3
/* In-place exponentiation to power 3 % key. */
static void modpow(const uint32_t *key,
		   const uint32_t *signature, uint32_t *out)
{
	static uint32_t aaR[RSA_NUM_WORDS];
	static uint32_t aaaR[RSA_NUM_WORDS];

	montMul(key, aaR, signature, signature);
	montMul(key, aaaR, aaR, signature);
	montMul1(key, out, aaaR);
}
#endif
void LOADERKEY_verify(uint32_t keyid, const uint32_t *signature,
		      const uint32_t *sha256)
{
	static uint32_t buf[RSA_NUM_WORDS]
		__attribute__((section(".guarded_data")));
	static uint32_t hash[SHA256_DIGEST_WORDS]
		__attribute__((section(".guarded_data")));
	uint32_t step, offset;
	const uint32_t *key;
	int i;

	if (keyid == LOADERKEY_B[0])
		key = LOADERKEY_B;
	else
		key = LOADERKEY_A;

	modpow(key, signature, buf);
	VERBOSE("sig %.384h\n", buf);

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
