/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "dcrypto.h"
#include "internal.h"

#include <assert.h>

#ifdef CONFIG_WATCHDOG
extern void watchdog_reload(void);
#else
static inline void watchdog_reload(void) { }
#endif

void bn_init(struct BIGNUM *b, void *buf, size_t len)
{
	/* Only word-multiple sized buffers accepted. */
	assert((len & 0x3) == 0);
	b->dmax = len / BN_BYTES;
	dcrypto_memset(buf, 0x00, len);
	b->d = (struct access_helper *) buf;
}

int bn_check_topbit(const struct BIGNUM *N)
{
	return BN_DIGIT(N, N->dmax - 1) >> 31;
}

/* a[n]. */
static int bn_is_bit_set(const struct BIGNUM *a, int n)
{
	int i, j;

	if (n < 0)
		return 0;

	i = n / BN_BITS2;
	j = n % BN_BITS2;
	if (a->dmax <= i)
		return 0;

	return (BN_DIGIT(a, i) >> j) & 1;
}

/* a[] >= b[]. */
/* TODO(ngm): constant time. */
static int bn_gte(const struct BIGNUM *a, const struct BIGNUM *b)
{
	int i;

	for (i = a->dmax - 1; BN_DIGIT(a, i) == BN_DIGIT(b, i) && i > 0; --i)
		;
	return BN_DIGIT(a, i) >= BN_DIGIT(b, i);
}

/* c[] = c[] - a[], assumes c > a. */
static uint32_t bn_sub(struct BIGNUM *c, const struct BIGNUM *a)
{
	int64_t A = 0;
	int i;

	for (i = 0; i < a->dmax; i++) {
		A += (uint64_t) BN_DIGIT(c, i) - BN_DIGIT(a, i);
		BN_DIGIT(c, i) = (uint32_t) A;
		A >>= 32;
	}
	return (uint32_t) A;  /* 0 or -1. */
}

/* c[] = c[] + a[]. */
static uint32_t bn_add(struct BIGNUM *c, const struct BIGNUM *a)
{
	uint64_t A = 0;
	int i;

	for (i = 0; i < a->dmax; ++i) {
		A += (uint64_t) BN_DIGIT(c, i) + BN_DIGIT(a, i);
		BN_DIGIT(c, i) = (uint32_t) A;
		A >>= 32;
	}

	return (uint32_t) A;  /* 0 or 1. */
}

/* r[] <<= 1. */
static uint32_t bn_lshift(struct BIGNUM *r)
{
	int i;
	uint32_t w;
	uint32_t carry = 0;

	for (i = 0; i < r->dmax; i++) {
		w = (BN_DIGIT(r, i) << 1) | carry;
		carry = BN_DIGIT(r, i) >> 31;
		BN_DIGIT(r, i) = w;
	}
	return carry;
}

/* Montgomery c[] += a * b[] / R % N. */
/* TODO(ngm): constant time. */
static void bn_mont_mul_add(struct BIGNUM *c, const uint32_t a,
			const struct BIGNUM *b,	const uint32_t nprime,
			const struct BIGNUM *N)
{
	uint32_t A, B, d0;
	int i;

	{
		register uint64_t tmp;

		tmp = BN_DIGIT(c, 0) + (uint64_t) a * BN_DIGIT(b, 0);
		A = tmp >> 32;
		d0 = (uint32_t) tmp * (uint32_t) nprime;
		tmp = (uint32_t)tmp + (uint64_t) d0 * BN_DIGIT(N, 0);
		B = tmp >> 32;
	}

	for (i = 0; i < N->dmax - 1;) {
		register uint64_t tmp;

		tmp = A + (uint64_t) a * BN_DIGIT(b, i + 1) +
			BN_DIGIT(c, i + 1);
		A = tmp >> 32;
		tmp = B + (uint64_t) d0 * BN_DIGIT(N, i + 1) + (uint32_t) tmp;
		BN_DIGIT(c, i) = (uint32_t) tmp;
		B = tmp >> 32;
		++i;
	}

	{
		uint64_t tmp = (uint64_t) A + B;

		BN_DIGIT(c, i) = (uint32_t) tmp;
		A = tmp >> 32;  /* 0 or 1. */
		if (A)
			bn_sub(c, N);
	}
}

/* Montgomery c[] = a[] * b[] / R % N. */
static void bn_mont_mul(struct BIGNUM *c, const struct BIGNUM *a,
			const struct BIGNUM *b, const uint32_t nprime,
			const struct BIGNUM *N)
{
	int i;

	for (i = 0; i < N->dmax; i++)
		BN_DIGIT(c, i) = 0;

	bn_mont_mul_add(c, a ? BN_DIGIT(a, 0) : 1, b, nprime, N);
	for (i = 1; i < N->dmax; i++)
		bn_mont_mul_add(c, a ? BN_DIGIT(a, i) : 0, b, nprime, N);
}

/* Mongomery R * R % N, R = 1 << (1 + log2N). */
/* TODO(ngm): constant time. */
static void bn_compute_RR(struct BIGNUM *RR, const struct BIGNUM *N)
{
	int i;

	bn_sub(RR, N);         /* R - N = R % N since R < 2N */

	/* Repeat 2 * R % N, log2(R) times. */
	for (i = 0; i < N->dmax * BN_BITS2; i++) {
		if (bn_lshift(RR))
			assert(bn_sub(RR, N) == -1);
		if (bn_gte(RR, N))
			bn_sub(RR, N);
	}
}

/* Montgomery nprime = -1 / n0 % (2 ^ 32). */
static uint32_t bn_compute_nprime(const uint32_t n0)
{
	int i;
	uint32_t ninv = 1;

	/* Repeated Hensel lifting. */
	for (i = 0; i  < 5; i++)
		ninv *= 2 - (n0 * ninv);

	return ~ninv + 1;       /* Two's complement. */
}

/* Montgomery output = input ^ exp % N. */
/* TODO(ngm): this implementation not timing or side-channel safe by
 * any measure. */
void bn_mont_modexp(struct BIGNUM *output, const struct BIGNUM *input,
		const struct BIGNUM *exp, const struct BIGNUM *N)
{
	int i;
	uint32_t nprime;
	uint32_t RR_buf[RSA_MAX_WORDS];
	uint32_t acc_buf[RSA_MAX_WORDS];
	uint32_t aR_buf[RSA_MAX_WORDS];

	struct BIGNUM RR;
	struct BIGNUM acc;
	struct BIGNUM aR;

	bn_init(&RR, RR_buf, bn_size(N));
	bn_init(&acc, acc_buf, bn_size(N));
	bn_init(&aR, aR_buf, bn_size(N));

	nprime = bn_compute_nprime(BN_DIGIT(N, 0));
	bn_compute_RR(&RR, N);
	bn_mont_mul(&acc, NULL, &RR, nprime, N);      /* R = 1 * RR / R % N */
	bn_mont_mul(&aR, input, &RR, nprime, N);      /* aR = a * RR / R % N */
	BN_DIGIT(output, 0) = 1;

	/* TODO(ngm): burn stack space and use windowing. */
	for (i = exp->dmax * BN_BITS2 - 1; i >= 0; i--) {
		bn_mont_mul(output, &acc, &acc, nprime, N);
		if (bn_is_bit_set(exp, i)) {
			bn_mont_mul(&acc, output, &aR, nprime, N);
		} else {
			struct BIGNUM tmp = *output;

			*output = acc;
			acc = tmp;
		}
		/* Poke the watchdog.
		 * TODO(ngm): may be unnecessary with
		 * a faster implementation.
		 */
		watchdog_reload();
	}

	bn_mont_mul(output, NULL, &acc, nprime, N);     /* Convert out. */
	/* Copy to output buffer if necessary. */
	if (acc.d != (struct access_helper *) acc_buf) {
		memcpy(acc.d, acc_buf, bn_size(output));
		*output = acc;
	}

	if (bn_sub(output, N))
		bn_add(output, N);                      /* Final reduce. */
	output->dmax = N->dmax;

	dcrypto_memset(RR_buf, 0, sizeof(RR_buf));
	dcrypto_memset(acc_buf, 0, sizeof(acc_buf));
	dcrypto_memset(aR_buf, 0, sizeof(aR_buf));
}
