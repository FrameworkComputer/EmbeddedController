/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifdef PRINT_PRIMES
#include "console.h"
#endif

#include "dcrypto.h"
#include "internal.h"

#include "trng.h"

#include "cryptoc/util.h"

#include <assert.h>

#ifdef CONFIG_WATCHDOG
extern void watchdog_reload(void);
#else
static inline void watchdog_reload(void) { }
#endif

void bn_init(struct LITE_BIGNUM *b, void *buf, size_t len)
{
	DCRYPTO_bn_wrap(b, buf, len);
	always_memset(buf, 0x00, len);
}

void DCRYPTO_bn_wrap(struct LITE_BIGNUM *b, void *buf, size_t len)
{
	/* Only word-multiple sized buffers accepted. */
	assert((len & 0x3) == 0);
	b->dmax = len / LITE_BN_BYTES;
	b->d = (struct access_helper *) buf;
}

int bn_eq(const struct LITE_BIGNUM *a, const struct LITE_BIGNUM *b)
{
	int i;
	uint32_t top = 0;

	for (i = a->dmax - 1; i > b->dmax - 1; --i)
		top |= BN_DIGIT(a, i);
	if (top)
		return 0;

	for (i = b->dmax - 1; i > a->dmax - 1; --i)
		top |= BN_DIGIT(b, i);
	if (top)
		return 0;

	for (i = MIN(a->dmax, b->dmax) - 1; i >= 0; --i)
		if (BN_DIGIT(a, i) != BN_DIGIT(b, i))
			return 0;

	return 1;
}

static void bn_copy(struct LITE_BIGNUM *dst, const struct LITE_BIGNUM *src)
{
	dst->dmax = src->dmax;
	memcpy(dst->d, src->d, bn_size(dst));
}

int bn_check_topbit(const struct LITE_BIGNUM *N)
{
	return BN_DIGIT(N, N->dmax - 1) >> 31;
}

/* a[n]. */
int bn_is_bit_set(const struct LITE_BIGNUM *a, int n)
{
	int i, j;

	if (n < 0)
		return 0;

	i = n / LITE_BN_BITS2;
	j = n % LITE_BN_BITS2;
	if (a->dmax <= i)
		return 0;

	return (BN_DIGIT(a, i) >> j) & 1;
}

static int bn_set_bit(const struct LITE_BIGNUM *a, int n)
{
	int i, j;

	if (n < 0)
		return 0;

	i = n / LITE_BN_BITS2;
	j = n % LITE_BN_BITS2;
	if (a->dmax <= i)
		return 0;

	BN_DIGIT(a, i) |= 1 << j;
	return 1;
}

/* a[] >= b[]. */
/* TODO(ngm): constant time. */
static int bn_gte(const struct LITE_BIGNUM *a, const struct LITE_BIGNUM *b)
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
uint32_t bn_sub(struct LITE_BIGNUM *c, const struct LITE_BIGNUM *a)
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
static uint32_t bn_signed_sub(struct LITE_BIGNUM *c, int *c_neg,
		const struct LITE_BIGNUM *a, int a_neg)
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
uint32_t bn_add(struct LITE_BIGNUM *c, const struct LITE_BIGNUM *a)
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
static uint32_t bn_signed_add(struct LITE_BIGNUM *c, int *c_neg,
			const struct LITE_BIGNUM *a, int a_neg)
{
	uint32_t A = bn_add(c, a);
	uint32_t carry;

	carry = (!*c_neg && !a_neg && A) || (*c_neg && a_neg && !A);
	*c_neg = carry ? *c_neg : (*c_neg + a_neg + A) & 0x01;
	return carry;
}

/* r[] <<= 1. */
static uint32_t bn_lshift(struct LITE_BIGNUM *r)
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
static void bn_rshift(struct LITE_BIGNUM *r, uint32_t carry, uint32_t neg)
{
	int i;
	uint32_t ones = ~0;
	uint32_t highbit = (!carry && neg) || (carry && !neg);

	for (i = 0; i < r->dmax - 1; ++i) {
		uint32_t accu;

		ones &= BN_DIGIT(r, i);
		accu = (BN_DIGIT(r, i) >> 1);
		accu |= (BN_DIGIT(r, i + 1) << (LITE_BN_BITS2 - 1));
		BN_DIGIT(r, i) = accu;
	}
	ones &= BN_DIGIT(r, i);
	BN_DIGIT(r, i) = (BN_DIGIT(r, i) >> 1) |
		(highbit << (LITE_BN_BITS2 - 1));

	if (ones == ~0 && highbit && neg)
		memset(r->d, 0x00, bn_size(r));    /* -1 >> 1 = 0. */
}

/* Montgomery c[] += a * b[] / R % N. */
/* TODO(ngm): constant time. */
static void bn_mont_mul_add(struct LITE_BIGNUM *c, const uint32_t a,
			const struct LITE_BIGNUM *b, const uint32_t nprime,
			const struct LITE_BIGNUM *N)
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
static void bn_mont_mul(struct LITE_BIGNUM *c, const struct LITE_BIGNUM *a,
			const struct LITE_BIGNUM *b, const uint32_t nprime,
			const struct LITE_BIGNUM *N)
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
static void bn_compute_RR(struct LITE_BIGNUM *RR, const struct LITE_BIGNUM *N)
{
	int i;

	bn_sub(RR, N);         /* R - N = R % N since R < 2N */

	/* Repeat 2 * R % N, log2(R) times. */
	for (i = 0; i < N->dmax * LITE_BN_BITS2; i++) {
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

/* TODO(ngm): this implementation not timing or side-channel safe by
 * any measure. */
static void bn_modexp_internal(struct LITE_BIGNUM *output,
				const struct LITE_BIGNUM *input,
				const struct LITE_BIGNUM *exp,
				const struct LITE_BIGNUM *N)
{
	int i;
	uint32_t nprime;
	uint32_t RR_buf[RSA_MAX_WORDS];
	uint32_t acc_buf[RSA_MAX_WORDS];
	uint32_t aR_buf[RSA_MAX_WORDS];

	struct LITE_BIGNUM RR;
	struct LITE_BIGNUM acc;
	struct LITE_BIGNUM aR;

	bn_init(&RR, RR_buf, bn_size(N));
	bn_init(&acc, acc_buf, bn_size(N));
	bn_init(&aR, aR_buf, bn_size(N));

	nprime = bn_compute_nprime(BN_DIGIT(N, 0));
	bn_compute_RR(&RR, N);
	bn_mont_mul(&acc, NULL, &RR, nprime, N);      /* R = 1 * RR / R % N */
	bn_mont_mul(&aR, input, &RR, nprime, N);      /* aR = a * RR / R % N */

	/* TODO(ngm): burn stack space and use windowing. */
	for (i = exp->dmax * LITE_BN_BITS2 - 1; i >= 0; i--) {
		bn_mont_mul(output, &acc, &acc, nprime, N);
		if (bn_is_bit_set(exp, i)) {
			bn_mont_mul(&acc, output, &aR, nprime, N);
		} else {
			struct LITE_BIGNUM tmp = *output;

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

	/* TODO(ngm): constant time. */
	if (bn_sub(output, N))
		bn_add(output, N);                      /* Final reduce. */
	output->dmax = N->dmax;

	always_memset(RR_buf, 0, sizeof(RR_buf));
	always_memset(acc_buf, 0, sizeof(acc_buf));
	always_memset(aR_buf, 0, sizeof(aR_buf));
}

/* output = input ^ exp % N */
int bn_modexp(struct LITE_BIGNUM *output, const struct LITE_BIGNUM *input,
		const struct LITE_BIGNUM *exp, const struct LITE_BIGNUM *N)
{
#ifndef CR50_NO_BN_ASM
	if ((bn_bits(N) & 255) == 0) {
		/* Use hardware support for standard key sizes. */
		return dcrypto_modexp(output, input, exp, N);
	}
#endif
	bn_modexp_internal(output, input, exp, N);
	return 1;
}

/* output = input ^ exp % N */
int bn_modexp_word(struct LITE_BIGNUM *output, const struct LITE_BIGNUM *input,
		uint32_t exp, const struct LITE_BIGNUM *N)
{
#ifndef CR50_NO_BN_ASM
	if ((bn_bits(N) & 255) == 0) {
		/* Use hardware support for standard key sizes. */
		return dcrypto_modexp_word(output, input, exp, N);
	}
#endif
	{
	struct LITE_BIGNUM pubexp;

	DCRYPTO_bn_wrap(&pubexp, &exp, sizeof(exp));
	bn_modexp_internal(output, input, &pubexp, N);
	return 1;
	}
}

/* output = input ^ exp % N */
int bn_modexp_blinded(struct LITE_BIGNUM *output,
			const struct LITE_BIGNUM *input,
			const struct LITE_BIGNUM *exp,
			const struct LITE_BIGNUM *N,
			uint32_t pubexp)
{
#ifndef CR50_NO_BN_ASM
	if ((bn_bits(N) & 255) == 0) {
		/* Use hardware support for standard key sizes. */
		return dcrypto_modexp_blinded(output, input, exp, N, pubexp);
	}
#endif
	bn_modexp_internal(output, input, exp, N);
	return 1;
}

/* c[] += a * b[] */
static uint32_t bn_mul_add(struct LITE_BIGNUM *c, uint32_t a,
		const struct LITE_BIGNUM *b, uint32_t offset)
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
void DCRYPTO_bn_mul(struct LITE_BIGNUM *c, const struct LITE_BIGNUM *a,
		const struct LITE_BIGNUM *b)
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

/* c[] = a[] * b[] */
static void bn_mul_ex(struct LITE_BIGNUM *c,
		const struct LITE_BIGNUM *a, int a_len,
		const struct LITE_BIGNUM *b)
{
	int i;
	uint32_t carry = 0;

	memset(c->d, 0, bn_size(c));
	for (i = 0; i < a_len; i++) {
		BN_DIGIT(c, i + b->dmax - 1) = carry;
		carry = bn_mul_add(c, BN_DIGIT(a, i), b, i);
	}

	BN_DIGIT(c, i + b->dmax - 1) = carry;
}

static int bn_div_word_ex(struct LITE_BIGNUM *q,
		struct LITE_BIGNUM *r,
		const struct LITE_BIGNUM *u, int m,
		uint32_t div)
{
	uint32_t rem = 0;
	int i;

	for (i = m - 1; i >= 0; --i) {
		uint64_t tmp = ((uint64_t)rem << 32) + BN_DIGIT(u, i);
		uint32_t qd = tmp / div;

		BN_DIGIT(q, i) = qd;
		rem = tmp - (uint64_t)qd * div;
	}

	if (r != NULL)
		BN_DIGIT(r, 0) = rem;

	return 1;
}

/*
 * Knuth's long division.
 *
 * Returns 0 on error.
 * |u| >= |v|
 * v[n-1] must not be 0
 * r gets |v| digits written to.
 * q gets |u| - |v| + 1 digits written to.
 */
static int bn_div_ex(struct LITE_BIGNUM *q,
		struct LITE_BIGNUM *r,
		const struct LITE_BIGNUM *u, int m,
		const struct LITE_BIGNUM *v, int n)
{
	uint32_t vtop;
	int s, i, j;
	uint32_t vn[RSA_MAX_WORDS]; /* Normalized v */
	uint32_t un[RSA_MAX_WORDS + 1]; /* Normalized u */

	if (m < n || n <= 0)
		return 0;

	vtop = BN_DIGIT(v, n - 1);

	if (vtop == 0)
		return 0;

	if (n == 1)
		return bn_div_word_ex(q, r, u, m, vtop);

	/* Compute shift factor to make v have high bit set */
	s = 0;
	while ((vtop & 0x80000000) == 0) {
		s = s + 1;
		vtop = vtop << 1;
	}

	/* Normalize u and v into un and vn.
	 * Note un always gains a leading digit
	 */
	if (s != 0) {
		for (i = n - 1; i > 0; i--)
			vn[i] = (BN_DIGIT(v, i) << s) |
				(BN_DIGIT(v, i - 1) >> (32 - s));
		vn[0] = BN_DIGIT(v, 0) << s;

		un[m] = BN_DIGIT(u, m - 1) >> (32 - s);
		for (i = m - 1; i > 0; i--)
			un[i] = (BN_DIGIT(u, i) << s) |
			  (BN_DIGIT(u, i - 1) >> (32 - s));
		un[0] = BN_DIGIT(u, 0) << s;
	} else {
		for (i = 0; i < n; ++i)
			vn[i] = BN_DIGIT(v, i);
		for (i = 0; i < m; ++i)
			un[i] = BN_DIGIT(u, i);
		un[m] = 0;
	}

	/* Main loop, reducing un digit by digit */
	for (j = m - n; j >= 0; j--) {
		uint32_t qd;
		int64_t t, k;

		/* Estimate quotient digit */
		if (un[j + n] == vn[n - 1]) {
			/* Maxed out */
			qd = 0xFFFFFFFF;
		} else {
			/* Fine tune estimate */
			uint64_t rhat = ((uint64_t)un[j + n] << 32) +
				un[j + n - 1];

			qd = rhat / vn[n - 1];
			rhat = rhat - (uint64_t)qd * vn[n - 1];
			while ((rhat >> 32) == 0 &&
				(uint64_t)qd * vn[n - 2] >
					(rhat << 32) + un[j + n - 2]) {
				qd = qd - 1;
				rhat = rhat + vn[n - 1];
			}
		}

		/* Multiply and subtract */
		k = 0;
		for (i = 0; i < n; i++) {
			uint64_t p = (uint64_t)qd * vn[i];

			t = un[i + j] - k - (p & 0xFFFFFFFF);
			un[i + j] = t;
			k = (p >> 32) - (t >> 32);
		}
		t = un[j + n] - k;
		un[j + n] = t;

		/* If borrowed, add one back and adjust estimate */
		if (t < 0) {
			k = 0;
			qd = qd - 1;
			for (i = 0; i < n; i++) {
				t = (uint64_t)un[i + j] + vn[i] + k;
				un[i + j] = t;
				k = t >> 32;
			}
			un[j + n] = un[j + n] + k;
		}

		BN_DIGIT(q, j) = qd;
	}

	if (r != NULL) {
		/* Denormalize un into r */
		if (s != 0) {
			for (i = 0; i < n - 1; i++)
				BN_DIGIT(r, i) = (un[i] >> s) |
					(un[i + 1] << (32 - s));
			BN_DIGIT(r, n - 1) = un[n - 1] >> s;
		} else {
			for (i = 0; i < n; i++)
				BN_DIGIT(r, i) = un[i];
		}
	}

	return 1;
}

static void bn_set_bn(struct LITE_BIGNUM *d, const struct LITE_BIGNUM *src,
		size_t n)
{
	size_t i = 0;

	for (; i < n && i < d->dmax; ++i)
		BN_DIGIT(d, i) = BN_DIGIT(src, i);
	for (; i < d->dmax; ++i)
		BN_DIGIT(d, i) = 0;
}

static size_t bn_digits(const struct LITE_BIGNUM *a)
{
	size_t n = a->dmax - 1;

	while (BN_DIGIT(a, n) == 0 && n)
		--n;
	return n + 1;
}

int DCRYPTO_bn_div(struct LITE_BIGNUM *quotient,
		struct LITE_BIGNUM *remainder,
		const struct LITE_BIGNUM *src,
		const struct LITE_BIGNUM *divisor)
{
	int src_len = bn_digits(src);
	int div_len = bn_digits(divisor);
	int i, result;

	if (src_len < div_len)
		return 0;

	result = bn_div_ex(quotient, remainder,
			src, src_len,
			divisor, div_len);

	if (!result)
		return 0;

	/* 0-pad the destinations. */
	for (i = src_len - div_len + 1; i < quotient->dmax; ++i)
		BN_DIGIT(quotient, i) = 0;
	if (remainder) {
		for (i = div_len; i < remainder->dmax; ++i)
			BN_DIGIT(remainder, i) = 0;
	}

	return result;
}

/*
 * Extended Euclid modular inverse.
 *
 * https://en.wikipedia.org/wiki/Extended_Euclidean_algorithm
 * #Computing_multiplicative_inverses_in_modular_structures:

 * function inverse(a, n)
 *  t := 0;     newt := 1;
 *  r := n;     newr := a;
 *  while newr ≠ 0
 *      quotient := r div newr
 *      (t, newt) := (newt, t - quotient * newt)
 *      (r, newr) := (newr, r - quotient * newr)
 *  if r > 1 then return "a is not invertible"
 *  if t < 0 then t := t + n
 *  return t
 */
int bn_modinv_vartime(struct LITE_BIGNUM *dst, const struct LITE_BIGNUM *src,
		const struct LITE_BIGNUM *mod)
{
	uint32_t R_buf[RSA_MAX_WORDS];
	uint32_t nR_buf[RSA_MAX_WORDS];
	uint32_t Q_buf[RSA_MAX_WORDS];

	uint32_t nT_buf[RSA_MAX_WORDS + 1]; /* Can go negative, hence +1 */
	uint32_t T_buf[RSA_MAX_WORDS + 1]; /* Can go negative */
	uint32_t tmp_buf[2 * RSA_MAX_WORDS + 1]; /* needs to hold Q*nT */

	struct LITE_BIGNUM R;
	struct LITE_BIGNUM nR;
	struct LITE_BIGNUM Q;
	struct LITE_BIGNUM T;
	struct LITE_BIGNUM nT;
	struct LITE_BIGNUM tmp;

	struct LITE_BIGNUM *pT = &T;
	struct LITE_BIGNUM *pnT = &nT;
	struct LITE_BIGNUM *pR = &R;
	struct LITE_BIGNUM *pnR = &nR;
	struct LITE_BIGNUM *bnswap;

	int t_neg = 0;
	int nt_neg = 0;
	int iswap;

	size_t r_len, nr_len;

	bn_init(&R, R_buf, bn_size(mod));
	bn_init(&nR, nR_buf, bn_size(mod));
	bn_init(&Q, Q_buf, bn_size(mod));
	bn_init(&T, T_buf, bn_size(mod) + sizeof(uint32_t));
	bn_init(&nT, nT_buf, bn_size(mod) + sizeof(uint32_t));
	bn_init(&tmp, tmp_buf, bn_size(mod) + sizeof(uint32_t));

	r_len = bn_digits(mod);
	nr_len = bn_digits(src);

	BN_DIGIT(&nT, 0) = 1;              /* T = 0, nT = 1 */
	bn_set_bn(&R, mod, r_len);         /* R = n */
	bn_set_bn(&nR, src, nr_len);       /* nR = input */

	/* Trim nR */
	while (nr_len && BN_DIGIT(&nR, nr_len - 1) == 0)
		--nr_len;

	while (nr_len) {
		size_t q_len = r_len - nr_len + 1;

		/* (r, nr) = (nr, r % nr), q = r / nr */
		if (!bn_div_ex(&Q, pR, pR, r_len, pnR, nr_len))
			return 0;

		/* swap R and nR */
		r_len = nr_len;
		bnswap = pR; pR = pnR; pnR = bnswap;

		/* trim nR and Q */
		while (nr_len && BN_DIGIT(pnR, nr_len - 1) == 0)
			--nr_len;
		while (q_len && BN_DIGIT(&Q, q_len - 1) == 0)
			--q_len;

		Q.dmax = q_len;

		/* compute t - q*nt */
		if (q_len == 1 && BN_DIGIT(&Q, 0) <= 2) {
			/* Doing few direct subs is faster than mul + sub */
			uint32_t n = BN_DIGIT(&Q, 0);

			while (n--)
				bn_signed_sub(pT, &t_neg, pnT, nt_neg);
		} else {
			/* Call bn_mul_ex with smallest operand first */
			if (nt_neg) {
				/* Negative numbers use all digits,
				 * thus pnT is large
				 */
				bn_mul_ex(&tmp, &Q, q_len, pnT);
			} else {
				int nt_len = bn_digits(pnT);

				if (q_len < nt_len)
					bn_mul_ex(&tmp, &Q, q_len, pnT);
				else
					bn_mul_ex(&tmp, pnT, nt_len, &Q);
			}
			bn_signed_sub(pT, &t_neg, &tmp, nt_neg);
		}

		/* swap T and nT */
		bnswap = pT; pT = pnT; pnT = bnswap;
		iswap = t_neg; t_neg = nt_neg; nt_neg = iswap;
	}

	if (r_len != 1 || BN_DIGIT(pR, 0) != 1) {
		/* gcd not 1; no direct inverse */
		return 0;
	}

	if (t_neg)
		bn_signed_add(pT, &t_neg, mod, 0);

	bn_set_bn(dst, pT, bn_digits(pT));

	return 1;
}

#define PRIME1 3

/*
 * The array below is an encoding of the first 4096 primes, starting with
 * PRIME1. Using 4096 of the first primes results in at least 5% improvement
 * in running time over using the first 2048.
 *
 * Most byte entries in the array contain two sequential differentials between
 * two adjacent prime numbers, each differential halved (as the difference is
 * always even) and packed into 4 bits.
 *
 * If a halved differential value exceeds 0xf (and as such does not fit into 4
 * bits), a zero is placed in the array followed by the value literal (no
 * halving).
 *
 * If out of two consecutive differencials only the second one exceeds 0xf,
 * the first one still is put into the array in its own byte prepended by a
 * zero.
 */
const uint8_t PRIME_DELTAS[] = {
	  1,  18,  18,  18,  49,  50,  18,  51,  19,  33,  50,  52,
	 33,  33,  39,  35,  21,  19,  50,  51,  21,  18,  22,  98,
	 18,  49,  83,  51,  19,  33,  87,  33,  39,  53,  18,  52,
	 51,  35,  66,  69,  21,  19,  35,  66,  18, 100,  36,  35,
	 97, 147,  83,  49,  53,  51,  19,  50,  22,  81,  35,  49,
	 98,  52,  84,  84,  51,  36,  50,  66, 117,  97,  81,  33,
	 87,  33,  39,  33,  42,  36,  84,  35,  55,  35,  52,  54,
	 35,  21,  19,  81,  81,  57,  33,  35,  52,  51, 177,  84,
	 83,  52,  98,  51,  19, 101, 145,  35,  19,  33,  38,  19,
	  0,  34,  51,  73,  87,  33,  35,  66,  19, 101,  18,  18,
	 54, 100,  99,  35,  66,  66, 114,  49,  35,  19,  90,  50,
	 28,  33,  86,  21,  67,  51, 147,  33, 101, 100, 135,  50,
	 18,  21,  99,  57,  24,  27,  52,  50,  18,  67,  81,  87,
	 83,  97,  33,  86,  24,  19,  33,  84, 156,  35,  72,  18,
	 72,  18,  67,  50,  97, 179,  19,  35, 115,  33,  50,  54,
	 51, 114,  54,  67,  45, 149,  66,  49,  59,  97, 132,  38,
	117,  18,  67,  50,  18,  52,  33,  53,  21,  66, 117,  97,
	 50,  24, 114,  52,  50, 148,  83,  52,  86, 114,  51,  30,
	 21,  66, 114,  70,  54,  35, 165,  24, 210,  22,  50,  99,
	 66,  75,  18,  22, 225,  51,  50,  49,  98,  97,  81, 129,
	131, 168,  66,  18,  27,  70,  53,  18,  49,  53,  22,  81,
	 87,  50,  52,  51, 134,  18, 115,  36,  84,  51, 179,  21,
	114,  57,  21, 114,  21, 114,  73,  35,  18,  49,  98, 171,
	 97,  35,  49,  59,  19, 131,  97,  54, 129,  35, 114,  25,
	197,  49,  81,  81,  83,  21,  21,  52, 245,  21,  67,  89,
	 54,  97, 147,  35,  57,  21, 115,  33,  44,  22,  56,  67,
	 57, 129,  35,  19,  53,  54, 105,  19,  41,  76,  33,  35,
	 22,  39, 245,  54, 115,  86,  18,  52,  53,  18, 115,  50,
	 49,  81, 134,  73,  35,  97,  51,  62,  55,  36,  84, 105,
	 33,  44,  99,  24,  51, 117, 114, 243,  51,  67,  33,  99,
	 33,  59,  49,  41,  18,  97,  50, 211,  50,  69,   0,  32,
	129,  50,  18,  21, 115,  36,  83, 162,  19, 242,  69,  51,
	 67,  98,  49,  50,  49,  81, 131, 162, 103, 227, 162, 148,
	 50,  55,  51,  81,  86,  69,  21,  70,  92,  18,  67,  36,
	149,  51,  19,  86,  21,  51,  52,  53,  49,  51,  53,  76,
	 59,  25,  36,  95,  73,  33,  83,  19,  41,  70, 152,  49,
	 99,  81,  81,  53, 114, 193, 129,  81,  90,  33,  36, 131,
	 49, 104,  66,  63,  21,  19,  35,  52,  50,  99,  70,  39,
	101, 195,  99,  27,  73,  83, 114,  19,  84,  50,  63, 117,
	 22,  81, 129, 156, 147, 137,  49, 146,  49,  84,  83,  52,
	 35,  21,  22,  35,  49,  98, 121,  35, 162,  67,  36,  39,
	 50, 118,  33, 242, 195,  54, 103,  50,  18, 147, 100,  50,
	 97, 111, 129,  59, 115,  86,  49,  36,  83,  60, 115,  36,
	105,  81,  81,  35, 163,  39,  33,  39,  54, 197,  52,  81,
	242,  49,  98, 115,   0,  34, 100,  53,  18, 165,  72,  21,
	114,  22,  56,  52,  36,  35,  67,  54,  50,  51,  73,  42,
	 38,  21,  49,  86,  18, 163, 243,  36,  86,  49, 225,  50,
	 24,  97,  53,  76,  99, 147,  39,  50, 100,  54,  35,  99,
	 97, 138,  33,  89,  66, 114,  19, 179, 115,  53,  49,  81,
	 33, 177,  35,  54,  55,  86,  52,   0,   4,   0,  36, 118,
	 50,  49,  99, 104,  21,  75,  22,  50,  57,  22,  50, 100,
	 54,  35,  99,  22,  98, 115, 131,  21,  73,   0,   6,   0,
	 34,  30,  27,  49,  86,  19,  36, 179,  21,  66,  52,  38,
	150, 162,  51,  66,  24,  97,  84,  81,  35, 118, 180, 225,
	 42,  33,  39,  86,  22, 129, 228, 180,  35,  55,  36,  99,
	 50, 162, 145,  99,  35, 121,  84,   0,  10,   0,  32,  53,
	 51,  19, 131,  22,  62,  21,  72,  52,  53, 202,  81,  81,
	 98,  58,  33, 105,  81,  81,  42, 141,  36,  50,  99,  70,
	 99,  36, 177, 135,  83, 102, 115,  42,  38,  49,  51, 132,
	177, 228,  50, 162, 108, 162,  69,  24,  22,   0,  12,   0,
	 34,  18,  54,  51,  67,  33,  60,  42,  83,  55,  35,  49,
	 99,  81,  83, 162, 210,  19, 177, 194,  49,  35, 195,  66,
	  0,   2,   0,  34,  52, 134,  21,  21,  52,  36, 107,  55,
	 45,  33, 101,  66,  70,  39,  56,  52,  35,  52,  53,  97,
	 51, 132,  51, 101,  19, 146,  51,  54, 148,  53,  73,  39,
	 57,  84,  86,  19, 102,   0,  36,  35,  66,  49,  41,  99,
	 67,  50, 145,  33, 194,  51, 127,  50,  54,  58,  36,  36,
	 51,  47,  21, 100,  84, 195,  98, 114,  49, 231, 129,  99,
	 42,  83,  51,  69, 103,  87, 135,  87,  56,  52,  56, 165,
	 19,  33,  38,  21,  19, 179,  18, 148,  84, 177,  89, 114,
	 18, 145,  35,  69,  31,  47,  21,  25,  41,  55,  81,  42,
	  0,  36,  50,  55,  42,  87, 179,  31, 101, 145,  39,  59,
	145,  99,  36,  36,  53,  22, 149, 120, 114,  51,  19,  33,
	225, 227,  18,  55,  38, 120, 114,  52,  50,  51,  52,  36,
	 39, 132,  50, 100, 129,  84,  35, 211,  84,  35, 103, 242,
	123,  70,  35,  69,  55,  83,  21, 102, 115,  57,  83,  73,
	 35,  19,  81,  84,  51,  81, 149,  22,  35,  69, 103,  98,
	 69,  51, 162, 120, 117,  69,  97, 147, 101,  97,  33,  99,
	 36,   0,   4,   0,  44,  33,  33,  86,  51, 114,  51,  52,
	  0,   6,   0,  36, 146,  49,  99,  51,  39, 182,  25,  83,
	220,  33,  33,  39,  35,  52, 134,   0,   2,   0,  42,  33,
	 44,  51,  25,  39,  62, 151,  53,  97,  54, 243,  35,  55,
	 33, 194,  51, 213, 147,  67,  63,  38,  97, 129,  50, 105,
	 19,  45,  99,  98, 204,  99,  22, 228,  35,  97, 147,  35,
	 58, 129,  51, 149,  49,  36,  51, 200,  52,  83, 123,  72,
	 49,  98,  27,  73,   0,  34,  19, 146,  51,  69,  73,  50,
	 18,  72,  22,  99, 146,  51,  49,  54,  90, 105,  35,  24,
	 21, 114, 241,  86,  28,  56,  69,  22, 179,  24, 165,  22,
	105,  86,  49,  81,  53, 145,  99,  35,  28, 225,  33,  81,
	134,  75,  19,  33,  83, 166,  84,  99,  51,  41,  18, 105,
	 22,  50,  24, 102, 114,  73,  38, 115,  50,  67,  42, 101,
	114,  24,  22, 242,  60, 172,  84, 101,  99, 102,  52, 135,
	 50,   0,   6,   0,  36, 165, 246,  18,  30, 103,  59,  66,
	147, 121,  35,  19,   0,  34, 145, 131, 145, 194,  19,  99,
	101,  67, 134,  69,   0,  14,   0,  40,  49,  50, 103,  33,
	 33,  36,  53,  51,  19,  51,  99, 197,  21,  54,  51, 115,
	  0,   6,   0,  52, 163,  81,  84,  86,  97,  50, 120,  70,
	 59,  21,  67, 177, 179,  69, 102,  21,  54,  18, 117,  19,
	146, 100, 150,  51,  35,  55,  33, 102,  35, 153,  97, 134,
	 73,  93,  35,  67,  50,  21, 162,  52,  42,  81,   0,  34,
	 18, 193, 102,  83,  22, 243, 104,  97, 185, 103,  81, 102,
	 33,  35,  97, 137,   0,   2,   0,  40,  72,  52,  81,  41,
	 69,  70,  41,  25,  81,  33,  36, 225,  59,  99, 121,  35,
	 67,  53,  66,  25,  83, 171,  67, 242,  18, 147, 241,  36,
	 50,  54,   0,  14,   0,  34, 115,  33,  50, 114,  19, 225,
	 35,  69,  21,  21,  18, 241, 102,  89, 103,  81,  99,  83,
	118,  39,  41,  21,  66,  69, 105, 148,  57, 135,  51,  87,
	 35,  22,  98,  51,  97, 129,  99,  39,  50,  22, 146,   0,
	 36, 150,  97,  33,  36,  98,   0,  36,  57,  22,  83, 108,
	 67,  56,  97, 149, 165,  19, 146,   0,   2,   0,  40,  49,
	129,  36, 149,  99,  21,  66,  54,  21, 148,  50, 162,   0,
	  6,   0,  36,  49,  83, 195, 120,  57,  21, 165,  67,  35,
	 21,  22,  33,  36,  83, 105, 118, 132,  56,  66,  19, 156,
	149,  97,  39,  83,  51, 150,  30, 151, 134, 124, 107,  49,
	 84,  33,  39,  99,  35, 114,  18, 243,  19,  81, 251,  18,
	 52,  51, 134,  99,  66,  28,  98,  52,  51,  81,  54, 231,
	 50, 100,  54,  35, 115, 101,  51,  67,  50,  18,  70,  39,
	149,  24,  58,  53,  66,   0,  30,   0,  36, 100, 182,  19,
	104,  51,  25,  45,  36, 149,  69,  55,  42, 185, 100, 230,
	 51,  67, 108, 135,  39,  99,  86, 163,  36, 150, 149,  18,
	165, 114,  49,  92, 145,  42, 135,  87,  50,  58,  53,  49,
	 99, 245,  67,  35,   0,   8,   0,  40,  18,  22, 146,  52,
	 83, 153,  22, 132,  50,  51,   0,   2,   0,  52, 114, 168,
	 18,  54,  19, 102,  50, 117,  51, 117, 120,  67,  98,  75,
	 49, 155,  49, 147, 135,  83,  97,  50,  73, 104,  18, 114,
	 70, 111, 132,  33,  59, 100,  83,  51, 115, 149,  97,  81,
	 45,  38,  66, 148,  87, 131,  52,  83,  67, 101, 165,  66,
	109, 146, 105,  63,  52,  59,  97,  35,  49,  81,  35,  49,
	 59, 147, 150,  70,  53,  97, 129,  81,  89,  58,  33,  59,
	 51, 147, 118, 129,  51,  39,  98,  25,   0,  16,   0,  36,
	 99, 126,  22,  54,  50,  24, 244, 195, 245,  25,  35, 100,
	177,  59, 145,  81,  95,  30,  55, 131, 168,  19,   0,   4,
	  0,  32,  33,  35,  22,  35,  54,  19,  35,  67,  42,   0,
	  4,   0,  32,  84, 129, 177,  35,  67, 135,  41,  66, 163,
	102,  53,  21,  22, 230, 145, 149,  69,   0,  48,  18,  52,
	 81,  95,   0,   2,   0,  36,  53,  49, 146,  52, 135, 131,
	114, 162,  49,  86,  19,  99,  50,  97,  50,  99,  66,  19,
	149,  52,  99, 177,  54, 146, 115,  42,  56,  66,  75,  70,
	 51, 134, 159,  66,  18,  61,  39, 203,  49,  53,  55,  51,
	101,  49, 101, 100, 153,  83,  72,  51,  72, 162,  21,  21,
	 99,  67,  90,  89, 210,  63,  18,  67, 102, 146,  75,  49,
	  0,  12,   0,  34,  57,  99,  30, 120, 114, 118,  35,  49,
	  0,  36,  35, 166, 195, 177, 137, 102, 145,  51,  50,  55,
	 33, 180,  99,  83,  70, 150,  53,  27, 115,  50, 147, 171,
	 22, 194, 153,  27,  18, 100, 101, 114,  25,   0,  16,   0,
	 38,  51,  54,  83, 100,  50,  55, 243,  84, 179,  70,  81,
	 81,  53,  21, 105, 163,  36, 179,  63,  55,  54,  99,  81,
	 95,  24,  66,  19, 146,  19,  45,  36,  53,  18,  52,  35,
	246,  19,  50, 171,  66,  18,   0,  72,  66,  75,  18, 117,
	 18, 163,  89,  58, 131,  67,  42, 107,  18,  22,  89,  27,
	 57, 241,  87,  84,   0,  16,   0,  50,  53,  69,  99, 145,
	179,  18,  52,  51,  89,  27,  24, 117,  49, 101, 162, 115,
	  0,   4,   0,  36,  18,  54,  18, 118,  50,  49,  50, 165,
	 21,  54,  28, 102,  51,  44,  18, 193,  50,  52, 131,  21,
	103,   0,   6,   0,  34,  55,  50,  31, 180,  35,  66,  30,
	 19,  45, 155,  19, 131,  24,  97,  98,  51, 117,  52,  98,
	145,  84, 131,  63,  21, 145,  84,  36, 108,   0,  40,  22,
	 83,  97,  98,  18,  57, 118,  50, 127,  36,  84,  53, 148,
	 39, 131,  66,  49,  81,  98,  18,  52,  35,   0,  32, 197,
	 73,  81,  53,  18, 147,  97, 129, 179,  52, 146, 150,  67,
	 42,  63, 182,  19, 146,   0,  62,  33,  99,  81, 102, 225,
	 39, 179,  19,  53, 114,  21,  52,  87,  83,  22, 185,  69,
	150,  22,  38,  21,  19, 147,   0,   6,   0,  34,  49,  98,
	 57, 145, 131,  52,  53, 148,  84,  81,  41, 214, 177,  33,
	179,  55, 131, 165,  97,   0,  18,   0,  42,  44,  19,  86,
	 19,  84,  35, 102,  66,  54, 250,  60,  53,  97,  90,  51,
	 38, 117, 150,  67,  98, 117,  22, 248,  22,  50,  18,  61,
	 41,  18,  55,   0,  54,   0,   6,   0,  52,  24,  51, 109,
	 33,  59,  49, 102,  53, 145, 102,  89,  99,  67,  83,  66,
	 18, 172,  51,  87,  81, 179, 117, 210, 148, 102,  86,  52,
	131,  67,  59,  21, 165,   0,   6,   0,  44, 147,  81,  35,
	114, 210,  22,  84,  36,  98, 100, 180,  53, 147,  52,  54,
	 36, 149,  99,  97,  50,  24, 102, 117, 115,  86,  22,  50,
	 49,  98, 211, 147,  83,  25,  84,  45,  90,  56, 166,  84,
	 81, 131, 165, 162, 241,  36, 129, 146,  19,  89, 103, 147,
	138,  50,  67,  35, 100,  81,  99,  33,  53,  24, 103,  83,
	 67, 225,  57,   0,  30,   0,  34,  24,  97, 152,  52,  84,
	 84,   0,  10,   0,  44,  51,  42,  33,  39, 228,  56, 127,
	 63,  39,  83,  52,  41,  99,  27, 100,  54,  39,  35,  18,
	154,  56,   0,  38, 129,  35,   0,   2,   0,  40,   0,  42,
	114,  49, 197,  49, 149,  97, 129,  56,  52,  33,  83,  69,
	 25, 132, 105,  99, 101,  51,
};

static uint32_t bn_mod_word16(const struct LITE_BIGNUM *p, uint16_t word)
{
	int i;
	uint32_t rem = 0;

	for (i = p->dmax - 1; i >= 0; i--) {
		rem = ((rem << 16) |
			((BN_DIGIT(p, i) >> 16) & 0xFFFFUL)) % word;
		rem = ((rem << 16) | (BN_DIGIT(p, i) & 0xFFFFUL)) % word;
	}

	return rem;
}

static uint32_t bn_mod_f4(const struct LITE_BIGNUM *d)
{
	int i = bn_size(d) - 1;
	const uint8_t *p = (const uint8_t *) (d->d);
	uint32_t rem = 0;

	for (; i >= 0; --i) {
		uint32_t q = RSA_F4 * (rem >> 8);

		if (rem < q)
			q -= RSA_F4;
		rem <<= 8;
		rem |= p[i];
		rem -= q;
	}

	if (rem >= RSA_F4)
		rem -= RSA_F4;

	return rem;
}

#define bn_is_even(b) !bn_is_bit_set((b), 0)
/* From HAC Fact 4.48 (ii), the following number of
 * rounds suffice for ~2^145 confidence.  Each additional
 * round provides about another k/100 bits of confidence. */
#define ROUNDS_1024 7
#define ROUNDS_512  15
#define ROUNDS_384  22

/* Miller-Rabin from HAC, algorithm 4.24. */
static int bn_probable_prime(const struct LITE_BIGNUM *p)
{
	int j;
	int s = 0;

	uint32_t ONE_buf = 1;
	uint8_t r_buf[RSA_MAX_BYTES / 2];
	uint8_t A_buf[RSA_MAX_BYTES / 2];
	uint8_t y_buf[RSA_MAX_BYTES / 2];

	struct LITE_BIGNUM ONE;
	struct LITE_BIGNUM r;
	struct LITE_BIGNUM A;
	struct LITE_BIGNUM y;

	const int rounds = bn_bits(p) >= 1024 ? ROUNDS_1024 :
			bn_bits(p) >= 512 ? ROUNDS_512 :
			ROUNDS_384;

	/* Failsafe: update rounds table above to support smaller primes. */
	if (bn_bits(p) < 384)
		return 0;

	if (bn_size(p) > sizeof(r_buf))
		return 0;

	DCRYPTO_bn_wrap(&ONE, &ONE_buf, sizeof(ONE_buf));
	DCRYPTO_bn_wrap(&r, r_buf, bn_size(p));
	bn_copy(&r, p);

	/* r * (2 ^ s) = p - 1 */
	bn_sub(&r, &ONE);
	while (bn_is_even(&r)) {
		bn_rshift(&r, 0, 0);
		s++;
	}

	DCRYPTO_bn_wrap(&A, A_buf, bn_size(p));
	DCRYPTO_bn_wrap(&y, y_buf, bn_size(p));
	for (j = 0; j < rounds; j++) {
		int i;

		/* pick random A, such that A < p */
		rand_bytes(A_buf, bn_size(&A));
		for (i = A.dmax - 1; i >= 0; i--) {
			while (BN_DIGIT(&A, i) > BN_DIGIT(p, i))
				BN_DIGIT(&A, i) = rand();
			if (BN_DIGIT(&A, i) < BN_DIGIT(p, i))
				break;
		}

		/* y = a ^ r mod p */
		bn_modexp(&y, &A, &r, p);
		if (bn_eq(&y, &ONE))
			continue;
		bn_add(&y, &ONE);
		if (bn_eq(&y, p))
			continue;
		bn_sub(&y, &ONE);

		/* y = y ^ 2 mod p */
		for (i = 0; i < s - 1; i++) {
			bn_copy(&A, &y);
			bn_modexp_word(&y, &A, 2, p);

			if (bn_eq(&y, &ONE))
				return 0;

			bn_add(&y, &ONE);
			if (bn_eq(&y, p)) {
				bn_sub(&y, &ONE);
				break;
			}
			bn_sub(&y, &ONE);
		}
		bn_add(&y, &ONE);
		if (!bn_eq(&y, p))
			return 0;
	}

	return 1;
}

/* #define PRINT_PRIMES to enable printing predefined prime numbers' set. */
static void print_primes(uint16_t prime)
{
#ifdef PRINT_PRIMES
	static uint16_t num_per_line;
	static uint16_t max_printed;

	if (prime <=  max_printed)
		return;

	if (!(num_per_line++ % 8)) {
		if (num_per_line == 1)
			ccprintf("Prime numbers:");
		ccprintf("\n");
		cflush();
	}
	max_printed = prime;
	ccprintf(" %6d", prime);
#endif
}

int DCRYPTO_bn_generate_prime(struct LITE_BIGNUM *p)
{
	int i;
	int j;
	/* Using a sieve size of 2048-bits results in a failure rate
	 * of ~0.5% @ 1024-bit candidates.  The failure rate rises to ~6%
	 * if the sieve size is halved. */
	uint8_t composites_buf[256];
	struct LITE_BIGNUM composites;
	uint16_t prime = PRIME1;

	/* Set top two bits, as well as LSB. */
	bn_set_bit(p, 0);
	bn_set_bit(p, bn_bits(p) - 1);
	bn_set_bit(p, bn_bits(p) - 2);

	/* Save on trial division by marking known composites. */
	bn_init(&composites, composites_buf, sizeof(composites_buf));
	for (i = 0; i < ARRAY_SIZE(PRIME_DELTAS); i++) {
		uint16_t rem;
		uint8_t unpacked_deltas[2];
		uint8_t packed_deltas = PRIME_DELTAS[i];
		int k;
		int m;

		if (packed_deltas) {
			unpacked_deltas[0] = (packed_deltas >> 4) << 1;
			unpacked_deltas[1] = (packed_deltas & 0xf) << 1;
			m = 2;
		} else {
			i += 1;
			unpacked_deltas[0] = PRIME_DELTAS[i];
			m = 1;
		}

		for (k = 0; k < m; k++) {
			prime += unpacked_deltas[k];
			print_primes(prime);
			rem = bn_mod_word16(p, prime);
			/* Skip marking odd offsets (i.e. even candidates). */
			for (j = (rem == 0) ? 0 : prime - rem;
			     j < bn_bits(&composites) << 1;
			     j += prime) {
				if ((j & 1) == 0)
					bn_set_bit(&composites, j >> 1);
			}
		}
	}

	/* composites now marked, apply Miller-Rabin to prime candidates. */
	j = 0;
	for (i = 0; i < bn_bits(&composites); i++) {
		uint32_t diff_buf;
		struct LITE_BIGNUM diff;

		if (bn_is_bit_set(&composites, i))
			continue;

		/* Recover increment from the composites sieve. */
		diff_buf = (i << 1) - j;
		j = (i << 1);
		DCRYPTO_bn_wrap(&diff, &diff_buf, sizeof(diff_buf));
		bn_add(p, &diff);
		/* Make sure prime will work with F4 public exponent. */
		if (bn_mod_f4(p) >= 2) {
			if (bn_probable_prime(p))
				return 1;
		}
	}

	always_memset(composites_buf, 0, sizeof(composites_buf));
	return 0;
}
