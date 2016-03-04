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
	DCRYPTO_bn_wrap(b, buf, len);
	dcrypto_memset(buf, 0x00, len);
}

void DCRYPTO_bn_wrap(struct BIGNUM *b, void *buf, size_t len)
{
	/* Only word-multiple sized buffers accepted. */
	assert((len & 0x3) == 0);
	b->dmax = len / BN_BYTES;
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
	uint32_t top = 0;

	for (i = a->dmax - 1; i > b->dmax - 1; --i)
		top |= BN_DIGIT(a, i);
	if (top)
		return 1;

	for (i = b->dmax - 1; i > a->dmax - 1; --i)
		top |= BN_DIGIT(b, i);
	if (top)
		return 0;

	for (i = MIN(a->dmax, b->dmax) - 1;
	     BN_DIGIT(a, i) == BN_DIGIT(b, i) && i > 0; --i)
		;
	return BN_DIGIT(a, i) >= BN_DIGIT(b, i);
}

/* c[] = c[] - a[], assumes c > a. */
uint32_t bn_sub(struct BIGNUM *c, const struct BIGNUM *a)
{
	int64_t A = 0;
	int i;

	for (i = 0; i < a->dmax; i++) {
		A += (uint64_t) BN_DIGIT(c, i) - BN_DIGIT(a, i);
		BN_DIGIT(c, i) = (uint32_t) A;
		A >>= 32;
	}

	for (; A && i < c->dmax; i++) {
		A += (uint64_t) BN_DIGIT(c, i);
		BN_DIGIT(c, i) = (uint32_t) A;
		A >>= 32;
	}

	return (uint32_t) A;  /* 0 or -1. */
}

/* c[] = c[] - a[], negative numbers in 2's complement representation. */
/* Returns borrow bit. */
static uint32_t bn_signed_sub(struct BIGNUM *c, int *c_neg,
		const struct BIGNUM *a, int a_neg)
{
	uint32_t carry = 0;
	uint64_t A = 1;
	int i;

	for (i = 0; i < a->dmax; ++i) {
		A += (uint64_t) BN_DIGIT(c, i) + ~BN_DIGIT(a, i);
		BN_DIGIT(c, i) = (uint32_t) A;
		A >>= 32;
	}

	for (; i < c->dmax; ++i) {
		A += (uint64_t) BN_DIGIT(c, i) + 0xFFFFFFFF;
		BN_DIGIT(c, i) = (uint32_t) A;
		A >>= 32;
	}

	A &= 0x01;
	carry = (!*c_neg && a_neg && A) || (*c_neg && !a_neg && !A);
	*c_neg = carry ? *c_neg : (*c_neg + !a_neg + A) & 0x01;
	return carry;
}

/* c[] = c[] + a[]. */
uint32_t bn_add(struct BIGNUM *c, const struct BIGNUM *a)
{
	uint64_t A = 0;
	int i;

	for (i = 0; i < a->dmax; ++i) {
		A += (uint64_t) BN_DIGIT(c, i) + BN_DIGIT(a, i);
		BN_DIGIT(c, i) = (uint32_t) A;
		A >>= 32;
	}

	for (; A && i < c->dmax; ++i) {
		A += (uint64_t) BN_DIGIT(c, i);
		BN_DIGIT(c, i) = (uint32_t) A;
		A >>= 32;
	}

	return (uint32_t) A;  /* 0 or 1. */
}

/* c[] = c[] + a[], negative numbers in 2's complement representation. */
/* Returns carry bit. */
static uint32_t bn_signed_add(struct BIGNUM *c, int *c_neg,
			const struct BIGNUM *a, int a_neg)
{
	uint32_t A = bn_add(c, a);
	uint32_t carry;

	carry = (!*c_neg && !a_neg && A) || (*c_neg && a_neg && !A);
	*c_neg = carry ? *c_neg : (*c_neg + a_neg + A) & 0x01;
	return carry;
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

/* r[] >>= 1.  Handles 2's complement negative numbers. */
static void bn_rshift(struct BIGNUM *r, uint32_t carry, uint32_t neg)
{
	int i;
	uint32_t ones = ~0;
	uint32_t highbit = (!carry && neg) || (carry && !neg);

	for (i = 0; i < r->dmax - 1; ++i) {
		uint32_t accu;

		ones &= BN_DIGIT(r, i);
		accu = (BN_DIGIT(r, i) >> 1);
		accu |= (BN_DIGIT(r, i + 1) << (BN_BITS2 - 1));
		BN_DIGIT(r, i) = accu;
	}
	ones &= BN_DIGIT(r, i);
	BN_DIGIT(r, i) = (BN_DIGIT(r, i) >> 1) |
		(highbit << (BN_BITS2 - 1));

	if (ones == ~0 && highbit && neg)
		memset(r->d, 0x00, bn_size(r));    /* -1 >> 1 = 0. */
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

/* c[] += a * b[] */
static uint32_t bn_mul_add(struct BIGNUM *c, uint32_t a,
			const struct BIGNUM *b, uint32_t offset)
{
	int i;
	uint64_t carry = 0;

	for (i = 0; i < b->dmax; i++) {
		carry += BN_DIGIT(c, offset + i) +
			(uint64_t) BN_DIGIT(b, i) * a;
		BN_DIGIT(c, offset + i) = (uint32_t) carry;
		carry >>= 32;
	}

	return carry;
}

/* c[] = a[] * b[] */
void bn_mul(struct BIGNUM *c, const struct BIGNUM *a, const struct BIGNUM *b)
{
	int i;
	uint32_t carry = 0;

	memset(c->d, 0, bn_size(c));
	for (i = 0; i < a->dmax; i++) {
		BN_DIGIT(c, i + b->dmax - 1) = carry;
		carry = bn_mul_add(c, BN_DIGIT(a, i), b, i);
	}

	BN_DIGIT(c, i + b->dmax - 1) = carry;
}

#define bn_is_even(b) !bn_is_bit_set((b), 0)
#define bn_is_odd(b) bn_is_bit_set((b), 0)

static int bn_is_zero(const struct BIGNUM *a)
{
	int i, result = 0;

	for (i = 0; i < a->dmax; ++i)
		result |= BN_DIGIT(a, i);
	return !result;
}

/* d = (e ^ -1) mod MOD  */
/* TODO(ngm): this method is used in place of division to calculate
 * q = N/p, i.e.  q = p^-1 mod (N-1).  The buffer e may be
 * resized to uint32_t once division is implemented. */
int bn_modinv_vartime(struct BIGNUM *d, const struct BIGNUM *e,
		const struct BIGNUM *MOD)
{
	/* Buffers for B, D, and U must be as large as e. */
	uint32_t A_buf[RSA_MAX_WORDS];
	uint32_t B_buf[RSA_MAX_WORDS / 2];
	uint32_t C_buf[RSA_MAX_WORDS];
	uint32_t D_buf[RSA_MAX_WORDS / 2];
	uint32_t U_buf[RSA_MAX_WORDS / 2];
	uint32_t V_buf[RSA_MAX_WORDS];
	int a_neg = 0;
	int b_neg = 0;
	int c_neg = 0;
	int d_neg = 0;
	int carry1;
	int carry2;
	int i = 0;

	struct BIGNUM A;
	struct BIGNUM B;
	struct BIGNUM C;
	struct BIGNUM D;
	struct BIGNUM U;
	struct BIGNUM V;

	if (bn_size(e) > sizeof(U_buf))
		return 0;

	bn_init(&A, A_buf, bn_size(MOD));
	BN_DIGIT(&A, 0) = 1;
	bn_init(&B, B_buf, bn_size(MOD) / 2);
	bn_init(&C, C_buf, bn_size(MOD));
	bn_init(&D, D_buf, bn_size(MOD) / 2);
	BN_DIGIT(&D, 0) = 1;

	bn_init(&U, U_buf, bn_size(e));
	memcpy(U_buf, e->d, bn_size(e));

	bn_init(&V, V_buf, bn_size(MOD));
	memcpy(V_buf, MOD->d, bn_size(MOD));

	/* Binary extended GCD, as per Handbook of Applied
	 * Cryptography, 14.61. */
	for (i = 0;; i++) {
		carry1 = 0;
		carry2 = 0;
		if (bn_is_even(&U)) {
			bn_rshift(&U, 0, 0);
			if (bn_is_odd(&A) || bn_is_odd(&B)) {
				carry1 = bn_signed_add(&A, &a_neg, MOD, 0);
				carry2 = bn_signed_sub(&B, &b_neg, e, 0);
			}
			bn_rshift(&A, carry1, a_neg);
			bn_rshift(&B, carry2, b_neg);
		} else if (bn_is_even(&V)) {
			bn_rshift(&V, 0, 0);
			if (bn_is_odd(&C) || bn_is_odd(&D)) {
				carry1 = bn_signed_add(&C, &c_neg, MOD, 0);
				carry2 = bn_signed_sub(&D, &d_neg, e, 0);
			}
			bn_rshift(&C, carry1, c_neg);
			bn_rshift(&D, carry2, d_neg);
		} else {  /* U, V both odd. */
			if (bn_gte(&U, &V)) {
				assert(!bn_sub(&U, &V));
				if (bn_signed_sub(&A, &a_neg, &C, c_neg))
					bn_signed_add(&A, &a_neg, MOD, 0);
				if (bn_signed_sub(&B, &b_neg, &D, d_neg))
					bn_signed_add(&B, &b_neg, MOD, 0);
				if (bn_is_zero(&U))
					break;  /* done. */
			} else {
				assert(!bn_sub(&V, &U));
				if (bn_signed_sub(&C, &c_neg, &A, a_neg))
					bn_signed_add(&C, &c_neg, MOD, 0);
				if (bn_signed_sub(&D, &d_neg, &B, b_neg))
					bn_signed_add(&D, &d_neg, MOD, 0);
			}
		}
		if ((i + 1) % 1000 == 0)
			/* TODO(ngm): Poke the watchdog (only
			 * necessary for q = N/p).  Remove once
			 * division is implemented. */
			watchdog_reload();
	}

	BN_DIGIT(&V, 0) ^= 0x01;
	if (bn_is_zero(&V)) {
		while (c_neg)
			bn_signed_add(&C, &c_neg, MOD, 0);
		while (bn_gte(&C, MOD))
			bn_sub(&C, MOD);

		memcpy(d->d, C.d, bn_size(d));
		return 1;
	} else {
		return 0;  /* Inverse not found. */
	}
}
