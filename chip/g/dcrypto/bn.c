/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "dcrypto.h"
#include "internal.h"

#include "trng.h"

#include <assert.h>

#ifdef CONFIG_WATCHDOG
extern void watchdog_reload(void);
#else
static inline void watchdog_reload(void) { }
#endif

void bn_init(struct LITE_BIGNUM *b, void *buf, size_t len)
{
	DCRYPTO_bn_wrap(b, buf, len);
	dcrypto_memset(buf, 0x00, len);
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

/* Montgomery output = input ^ exp % N. */
/* TODO(ngm): this implementation not timing or side-channel safe by
 * any measure. */
void bn_mont_modexp(struct LITE_BIGNUM *output, const struct LITE_BIGNUM *input,
		const struct LITE_BIGNUM *exp, const struct LITE_BIGNUM *N)
{
	int i;
	uint32_t nprime;
	uint32_t RR_buf[RSA_MAX_WORDS];
	uint32_t acc_buf[RSA_MAX_WORDS];
	uint32_t aR_buf[RSA_MAX_WORDS];

	struct LITE_BIGNUM RR;
	struct LITE_BIGNUM acc;
	struct LITE_BIGNUM aR;

#ifndef CR50_NO_BN_ASM
	if ((bn_bits(N) & 255) == 0) {
		/* Use hardware support for standard key sizes. */
		bn_mont_modexp_asm(output, input, exp, N);
		return;
	}
#endif

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

	dcrypto_memset(RR_buf, 0, sizeof(RR_buf));
	dcrypto_memset(acc_buf, 0, sizeof(acc_buf));
	dcrypto_memset(aR_buf, 0, sizeof(aR_buf));
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

#define NUM_PRIMES 4095
#define PRIME1 3

/* First NUM_PRIMES worth of primes starting with PRIME1.  The entries
 * are a delta / 2 encoding, i.e.:
 *     prime(x) = prime(x - 1) + (PRIME_DELTAS[x] * 2)
 *
 * Using 4096 of the first primes results in a 5-10% improvement in
 * running time over using the first 2048. */
const uint8_t PRIME_DELTAS[NUM_PRIMES] = {
	    0,  1,  1,  2,  1,  2,  1,  2,  3,  1,  3,  2,  1,  2,  3,
	3,  1,  3,  2,  1,  3,  2,  3,  4,  2,  1,  2,  1,  2,  7,  2,
	3,  1,  5,  1,  3,  3,  2,  3,  3,  1,  5,  1,  2,  1,  6,  6,
	2,  1,  2,  3,  1,  5,  3,  3,  3,  1,  3,  2,  1,  5,  7,  2,
	1,  2,  7,  3,  5,  1,  2,  3,  4,  3,  3,  2,  3,  4,  2,  4,
	5,  1,  5,  1,  3,  2,  3,  4,  2,  1,  2,  6,  4,  2,  4,  2,
	3,  6,  1,  9,  3,  5,  3,  3,  1,  3,  5,  3,  3,  1,  3,  3,
	2,  1,  6,  5,  1,  2,  3,  3,  1,  6,  2,  3,  4,  5,  4,  5,
	4,  3,  3,  2,  4,  3,  2,  4,  2,  7,  5,  6,  1,  5,  1,  2,
	1,  5,  7,  2,  1,  2,  7,  2,  1,  2, 10,  2,  4,  5,  4,  2,
	3,  3,  7,  2,  3,  3,  4,  3,  6,  2,  3,  1,  5,  1,  3,  5,
	1,  5,  1,  3,  9,  2,  1,  2,  3,  3,  4,  3,  3, 11,  1,  5,
	4,  5,  3,  3,  4,  6,  2,  3,  3,  1,  3,  6,  5,  9,  1,  2,
	3,  1,  3,  2,  1,  2,  6,  1,  3, 17,  3,  3,  4,  9,  5,  7,
	2,  1,  2,  3,  4,  2,  1,  3,  6,  5,  1,  2,  1,  2,  3,  6,
	6,  4,  6,  3,  2,  3,  4,  2,  4,  2,  7,  2,  3,  1,  2,  3,
	1,  3,  5, 10,  3,  2,  1, 12,  2,  1,  5,  6,  1,  5,  4,  3,
	3,  3,  9,  3,  2,  1,  6,  5,  6,  4,  8,  7,  3,  2,  1,  2,
	1,  5,  6,  3,  3,  9,  1,  8,  1, 11,  3,  4,  3,  2,  1,  2,
	4,  3,  5,  1,  5,  7,  5,  3,  6,  1,  2,  1,  5,  6,  1,  8,
	1,  3,  2,  1,  5,  4,  9, 12,  2,  3,  4,  8,  1,  2,  4,  8,
	1,  2,  4,  3,  3,  2,  6,  1, 11,  3,  1,  3,  2,  3,  7,  3,
	2,  1,  3,  2,  3,  6,  3,  3,  7,  2,  3,  6,  4,  3,  2, 13,
	9,  5,  4,  2,  3,  1,  3, 11,  6,  1,  8,  4,  2,  6,  7,  5,
	1,  2,  4,  3,  3,  2,  1,  2,  3,  4,  2,  1,  3,  5,  1,  5,
	4,  2,  7,  5,  6,  1,  3,  2,  1,  8,  7,  2,  3,  4,  3,  2,
	9,  4,  5,  3,  3,  4,  5,  6,  7,  2,  3,  3,  1, 14,  1,  5,
	4,  2,  7,  2,  4,  6,  3,  6,  2,  3, 10,  5,  1,  8, 13,  2,
	1,  6,  3,  2,  6,  3,  4,  2,  4, 11,  1,  2,  1,  6, 14,  1,
	3,  3,  3,  2,  3,  1,  6,  2,  6,  1,  5,  1,  8,  1,  8,  3,
	10, 8,  4,  2,  1,  2,  1, 11,  4,  6,  3,  5,  1,  2,  3,  1,
	3,  5,  1,  6,  5,  1,  5,  7,  3,  2,  3,  4,  3,  3,  8,  6,
	1,  2,  7,  3,  2,  4,  5,  4,  3,  3, 11,  3,  1,  5,  7,  2,
	3,  9,  1,  5,  7,  2,  1,  5,  7,  2,  4,  9,  2,  3,  1,  2,
	3,  1,  6,  2, 10, 11,  6,  1,  2,  3,  3,  1,  3, 11,  1,  3,
	8,  3,  6,  1,  3,  6,  8,  1,  2,  3,  7,  2,  1,  9, 12,  5,
	3,  1,  5,  1,  5,  1,  5,  3,  1,  5,  1,  5,  3,  4, 15,  5,
	1,  5,  4,  3,  5,  9,  3,  6,  6,  1,  9,  3,  2,  3,  3,  9,
	1,  5,  7,  3,  2,  1,  2, 12,  1,  6,  3,  8,  4,  3,  3,  9,
	8,  1,  2,  3,  1,  3,  3,  5,  3,  6,  6,  9,  1,  3,  2,  9,
	4, 12,  2,  1,  2,  3,  1,  6,  2,  7, 15,  5,  3,  6,  7,  3,
	5,  6,  1,  2,  3,  4,  3,  5,  1,  2,  7,  3,  3,  2,  3,  1,
	5,  1,  8,  6,  4,  9,  2,  3,  6,  1,  3,  3,  3, 14,  3,  7,
	2,  4,  5,  4,  6,  9,  2,  1,  2, 12,  6,  3,  1,  8,  3,  3,
	7,  5,  7,  2, 15,  3,  3,  3,  4,  3,  2,  1,  6,  3,  2,  1,
	3, 11,  3,  1,  2,  9,  1,  2,  6,  1,  3,  2, 13,  3,  3,  2,
	4,  5, 16,  8,  1,  3,  2,  1,  2,  1,  5,  7,  3,  2,  4,  5,
	3, 10,  2,  1,  3, 15,  2,  4,  5,  3,  3,  4,  3,  6,  2,  3,
	1,  3,  2,  3,  1,  5,  1,  8,  3, 10,  2,  6,  7, 14,  3, 10,
	2,  9,  4,  3,  2,  3,  7,  3,  3,  5,  1,  5,  6,  4,  5,  1,
	5,  4,  6,  5, 12,  1,  2,  4,  3,  2,  4,  9,  5,  3,  3,  1,
	3,  5,  6,  1,  5,  3,  3,  3,  4,  3,  5,  3,  1,  3,  3,  3,
	5,  4, 12,  3, 11,  1,  9,  2,  4,  5, 15,  4,  9,  2,  1,  5,
	3,  1,  3,  2,  9,  4,  6,  9,  8,  3,  1,  6,  3,  5,  1,  5,
	1,  3,  5,  7,  2, 12,  1,  8,  1,  5,  1,  5, 10,  2,  1,  2,
	4,  8,  3,  3,  1,  6,  8,  4,  2,  3, 15,  1,  5,  1,  3,  2,
	3,  3,  4,  3,  2,  6,  3,  4,  6,  2,  7,  6,  5, 12,  3,  6,
	3,  1, 11,  4,  9,  5,  3,  7,  2,  1,  3,  5,  4,  3,  2,  3,
	15, 7,  5,  1,  6,  5,  1,  8,  1,  9, 12,  9,  3,  8,  9,  3,
	1,  9,  2,  3,  1,  5,  4,  5,  3,  3,  4,  2,  3,  1,  5,  1,
	6,  2,  3,  3,  1,  6,  2,  7,  9,  2,  3, 10,  2,  4,  3,  2,
	4,  2,  7,  3,  2,  7,  6,  2,  1, 15,  2, 12,  3,  3,  6,  6,
	7,  3,  2,  1,  2,  9,  3,  6,  4,  3,  2,  6,  1,  6, 15,  8,
	1,  3, 11,  7,  3,  5,  6,  3,  1,  2,  4,  5,  3,  3, 12,  7,
	3,  2,  4,  6,  9,  5,  1,  5,  1,  2,  3, 10,  3,  2,  7,  2,
	1,  2,  7,  3,  6, 12,  5,  3,  4,  5,  1, 15,  2,  3,  1,  6,
	2,  7,  3, 17,  6,  4,  3,  5,  1,  2, 10,  5,  4,  8,  1,  5,
	7,  2,  1,  6,  3,  8,  3,  4,  2,  4,  2,  3,  4,  3,  3,  6,
	3,  2,  3,  3,  4,  9,  2, 10,  2,  6,  1,  5,  3,  1,  5,  6,
	1,  2, 10,  3, 15,  3,  2,  4,  5,  6,  3,  1, 14,  1,  3,  2,
	1,  8,  6,  1,  3,  5,  4, 12,  6,  3,  9,  3,  2,  7,  3,  2,
	6,  4,  3,  6,  2,  3,  6,  3,  6,  1,  8, 10,  2,  1,  5,  9,
	4,  2,  7,  2,  1,  3, 11,  3,  7,  3,  3,  5,  3,  1,  5,  1,
	2,  1, 11,  1,  2,  3,  3,  6,  3,  7,  5,  6,  3,  4,  2, 18,
	7,  6,  3,  2,  3,  1,  6,  3,  6,  8,  1,  5,  4, 11,  1,  6,
	3,  2,  3,  9,  1,  6,  3,  2,  6,  4,  3,  6,  2,  3,  6,  3,
	1,  6,  6,  2,  7,  3,  8,  3,  1,  5,  4,  9,  3, 17,  1, 14,
	1, 11,  3,  1,  5,  6,  1,  3,  2,  4, 11,  3,  1,  5,  4,  2,
	3,  4,  2,  6,  9,  6, 10,  2,  3,  3,  4,  2,  1,  8,  6,  1,
	5,  4,  5,  1,  2,  3,  7,  6, 11,  4, 14,  1,  2, 10,  2,  1,
	2,  7,  5,  6,  1,  6,  8,  1, 14,  4, 11,  4,  2,  3,  3,  7,
	2,  4,  6,  3,  3,  2, 10,  2,  9,  1,  6,  3,  2,  3,  7,  9,
	5,  4,  5, 16,  3,  5,  3,  3,  1,  3,  8,  3,  1,  6,  3, 14,
	1,  5,  4,  8,  3,  4,  3,  5, 12, 10,  5,  1,  5,  1,  6,  2,
	3, 10,  2,  1,  6,  9,  5,  1,  5,  1,  2, 10,  8, 13,  2,  4,
	3,  2,  6,  3,  4,  6,  6,  3,  2,  4, 11,  1,  8,  7,  5,  3,
	6,  6,  7,  3,  2, 10,  2,  6,  3,  1,  3,  3,  8,  4, 11,  1,
	14, 4,  3,  2, 10,  2,  6, 12, 10,  2,  4,  5,  1,  8,  1,  6,
	6, 17,  1,  2,  3,  6,  3,  3,  4,  3,  2,  1,  3, 12,  2, 10,
	5,  3,  3,  7,  2,  3,  3,  1,  6,  3,  5,  1,  5,  3, 10,  2,
	13, 2,  1,  3, 11,  1, 12,  2,  3,  1,  2,  3, 12,  3,  4,  2,
	1, 17,  3,  4,  8,  6,  1,  5,  1,  5,  3,  4,  2,  4,  6, 11,
	3,  7,  2, 13,  2,  1,  6,  5,  4,  2,  4,  6,  2,  7,  3,  8,
	3,  4,  2,  3,  3,  4,  3,  5,  6,  1,  3,  3,  8,  4,  3,  3,
	6,  5,  1,  3,  9,  2,  3,  3,  3,  6,  9,  4,  3,  5,  4,  9,
	2,  7,  3,  9,  5,  4,  5,  6,  1,  3,  6,  6, 18,  2,  3,  4,
	2,  3,  1,  2,  9,  6,  3,  4,  3,  3,  2,  9,  1,  2,  1, 12,
	2,  3,  3,  7, 15,  3,  2,  3,  6,  3, 10,  2,  4,  2,  4,  3,
	3,  2, 15,  1,  5,  6,  4,  5,  4, 12,  3,  6,  2,  7,  2,  3,
	1, 14,  7,  8,  1,  6,  3,  2, 10,  5,  3,  3,  3,  4,  5,  6,
	7,  5,  7,  8,  7,  5,  7,  3,  8,  3,  4,  3,  8, 10,  5,  1,
	3,  2,  1,  2,  6,  1,  5,  1,  3, 11,  3,  1,  2,  9,  4,  5,
	4, 11,  1,  5,  9,  7,  2,  1,  2,  9,  1,  2,  3,  4,  5,  1,
	15, 2, 15,  1,  5,  1,  9,  2,  9,  3,  7,  5,  1,  2, 10, 18,
	3,  2,  3,  7,  2, 10,  5,  7, 11,  3,  1, 15,  6,  5,  9,  1,
	2,  7,  3, 11,  9,  1,  6,  3,  2,  4,  2,  4,  3,  5,  1,  6,
	9,  5,  7,  8,  7,  2,  3,  3,  1,  3,  2,  1, 14,  1, 14,  3,
	1,  2,  3,  7,  2,  6,  7,  8,  7,  2,  3,  4,  3,  2,  3,  3,
	3,  4,  2,  4,  2,  7,  8,  4,  3,  2,  6,  4,  8,  1,  5,  4,
	2,  3, 13,  3,  5,  4,  2,  3,  6,  7, 15,  2,  7, 11,  4,  6,
	2,  3,  4,  5,  3,  7,  5,  3,  1,  5,  6,  6,  7,  3,  3,  9,
	5,  3,  4,  9,  2,  3,  1,  3,  5,  1,  5,  4,  3,  3,  5,  1,
	9,  5,  1,  6,  2,  3,  4,  5,  6,  7,  6,  2,  4,  5,  3,  3,
	10, 2,  7,  8,  7,  5,  4,  5,  6,  1,  9,  3,  6,  5,  6,  1,
	2,  1,  6,  3,  2,  4,  2, 22,  2,  1,  2,  1,  5,  6,  3,  3,
	7,  2,  3,  3,  3,  4,  3, 18,  9,  2,  3,  1,  6,  3,  3,  3,
	2,  7, 11,  6,  1,  9,  5,  3, 13, 12,  2,  1,  2,  1,  2,  7,
	2,  3,  3,  4,  8,  6,  1, 21,  2,  1,  2, 12,  3,  3,  1,  9,
	2,  7,  3, 14,  9,  7,  3,  5,  6,  1,  3,  6, 15,  3,  2,  3,
	3,  7,  2,  1, 12,  2,  3,  3, 13,  5,  9,  3,  4,  3,  3, 15,
	2,  6,  6,  1,  8,  1,  3,  2,  6,  9,  1,  3,  2, 13,  6,  3,
	6,  2, 12, 12,  6,  3,  1,  6, 14,  4,  2,  3,  6,  1,  9,  3,
	2,  3,  3, 10,  8,  1,  3,  3,  9,  5,  3,  1,  2,  4,  3,  3,
	12, 8,  3,  4,  5,  3,  7, 11,  4,  8,  3,  1,  6,  2,  1, 11,
	4,  9, 17,  1,  3,  9,  2,  3,  3,  4,  5,  4,  9,  3,  2,  1,
	2,  4,  8,  1,  6,  6,  3,  9,  2,  3,  3,  3,  1,  3,  6,  5,
	10, 6,  9,  2,  3,  1,  8,  1,  5,  7,  2, 15,  1,  5,  6,  1,
	12, 3,  8,  4,  5,  1,  6, 11,  3,  1,  8, 10,  5,  1,  6,  6,
	9,  5,  6,  3,  1,  5,  1,  3,  5,  9,  1,  6,  3,  2,  3,  1,
	12, 14, 1,  2,  1,  5,  1,  8,  6,  4, 11,  1,  3,  2,  1,  5,
	3,  10, 6,  5,  4,  6,  3,  3,  3,  2,  9,  1,  2,  6,  9,  1,
	6,  3,  2,  1,  8,  6,  6,  7,  2,  4,  9,  2,  6,  7,  3,  3,
	2,  4,  3,  2, 10,  6,  5,  7,  2,  1,  8,  1,  6, 15,  2,  3,
	12, 10, 12, 5,  4,  6,  5,  6,  3,  6,  6,  3,  4,  8,  7,  3,
	2,  3, 18, 10,  5, 15,  6,  1,  2,  1, 14,  6,  7,  3, 11,  4,
	2,  9,  3,  7,  9,  2,  3,  1,  3, 17,  9,  1,  8,  3,  9,  1,
	12, 2,  1,  3,  6,  3,  6,  5,  4,  3,  8,  6,  4,  5,  7, 20,
	3,  1,  3,  2,  6,  7,  2,  1,  2,  1,  2,  4,  3,  5,  3,  3,
	1,  3,  3,  3,  6,  3, 12,  5,  1,  5,  3,  6,  3,  3,  7,  3,
	3, 26, 10,  3,  5,  1,  5,  4,  5,  6,  6,  1,  3,  2,  7,  8,
	4,  6,  3, 11,  1,  5,  4,  3, 11,  1, 11,  3,  4,  5,  6,  6,
	1,  5,  3,  6,  1,  2,  7,  5,  1,  3,  9,  2,  6,  4,  9,  6,
	3,  3,  2,  3,  3,  7,  2,  1,  6,  6,  2,  3,  9,  9,  6,  1,
	8,  6,  4,  9,  5, 13,  2,  3,  4,  3,  3,  2,  1,  5, 10,  2,
	3,  4,  2, 10,  5,  1, 17,  1,  2, 12,  1,  6,  6,  5,  3,  1,
	6, 15,  3,  6,  8,  6,  1, 11,  9,  6,  7,  5,  1,  6,  6,  2,
	1,  2,  3,  6,  1,  8,  9,  1, 20,  4,  8,  3,  4,  5,  1,  2,
	9,  4,  5,  4,  6,  2,  9,  1,  9,  5,  1,  2,  1,  2,  4, 14,
	1,  3, 11,  6,  3,  7,  9,  2,  3,  4,  3,  3,  5,  4,  2,  1,
	9,  5,  3, 10, 11,  4,  3, 15,  2,  1,  2,  9,  3, 15,  1,  2,
	4,  3,  2,  3,  6,  7, 17,  7,  3,  2,  1,  3,  2,  7,  2,  1,
	3, 14,  1,  2,  3,  4,  5,  1,  5,  1,  5,  1,  2, 15,  1,  6,
	6,  5,  9,  6,  7,  5,  1,  6,  3,  5,  3,  7,  6,  2,  7,  2,
	9,  1,  5,  4,  2,  4,  5,  6,  9,  9,  4,  3,  9,  8,  7,  3,
	3,  5,  7,  2,  3,  1,  6,  6,  2,  3,  3,  6,  1,  8,  1,  6,
	3,  2,  7,  3,  2,  1,  6,  9,  2, 18,  9,  6,  6,  1,  2,  1,
	2,  4,  6,  2, 18,  3,  9,  1,  6,  5,  3,  6, 12,  4,  3,  3,
	8,  6,  1,  9,  5, 10,  5,  1,  3,  9,  2,  1, 20,  3,  1,  8,
	1,  2,  4,  9,  5,  6,  3,  1,  5,  4,  2,  3,  6,  1,  5,  9,
	4,  3,  2, 10,  2,  3, 18,  3,  1,  5,  3, 12,  3,  7,  8,  3,
	9,  1,  5, 10,  5,  4,  3,  2,  3,  1,  5,  1,  6,  2,  1,  2,
	4,  5,  3,  6,  9,  7,  6,  8,  4,  3,  8,  4,  2,  1,  3,  9,
	12, 9,  5,  6,  1,  2,  7,  5,  3,  3,  3,  9,  6,  1, 14,  9,
	7,  8,  6,  7, 12,  6, 11,  3,  1,  5,  4,  2,  1,  2,  7,  6,
	3,  2,  3,  7,  2,  1,  2, 15,  3,  1,  3,  5,  1, 15, 11,  1,
	2,  3,  4,  3,  3,  8,  6,  6,  3,  4,  2,  1, 12,  6,  2,  3,
	4,  3,  3,  5,  1,  3,  6, 14,  7,  3,  2,  6,  4,  3,  6,  2,
	3,  7,  3,  6,  5,  3,  3,  4,  3,  3,  2,  1,  2,  4,  6,  2,
	7,  9,  5,  1,  8,  3, 10,  3,  5,  4,  2, 15, 18,  6,  4, 11,
	6,  1,  3,  6,  8,  3,  3,  1,  9,  2, 13,  2,  4,  9,  5,  4,
	5,  3,  7,  2, 10, 11,  9,  6,  4, 14,  6,  3,  3,  4,  3,  6,
	12, 8,  7,  2,  7,  6,  3,  5,  6, 10,  3,  2,  4,  9,  6,  9,
	5,  1,  2, 10,  5,  7,  2,  3,  1,  5, 12,  9,  1,  2, 10,  8,
	7,  5,  7,  3,  2,  3, 10,  3,  5,  3,  1,  6,  3, 15,  5,  4,
	3,  2,  3,  4, 20,  1,  2,  1,  6,  9,  2,  3,  4,  5,  3,  9,
	9,  1,  6,  8,  4,  3,  2,  3,  3,  1, 26,  7,  2, 10,  8,  1,
	2,  3,  6,  1,  3,  6,  6,  3,  2,  7,  5,  3,  3,  7,  5,  7,
	8,  4,  3,  6,  2,  4, 11,  3,  1,  9, 11,  3,  1,  9,  3,  8,
	7,  5,  3,  6,  1,  3,  2,  4,  9,  6,  8,  1,  2,  7,  2,  4,
	6,  6, 15,  8,  4,  2,  1,  3, 11,  6,  4,  5,  3,  3,  3,  7,
	3,  9,  5,  6,  1,  5,  1,  2, 13,  2,  6,  4,  2,  9,  4,  5,
	7,  8,  3,  3,  4,  5,  3,  4,  3,  6,  5, 10,  5,  4,  2,  6,
	13, 9,  2,  6,  9,  3, 15,  3,  4,  3, 11,  6,  1,  2,  3,  3,
	1,  5,  1,  2,  3,  3,  1,  3, 11,  9,  3,  9,  6,  4,  6,  3,
	5,  6,  1,  8,  1,  5,  1,  5,  9,  3, 10,  2,  1,  3, 11,  3,
	3,  9,  3,  7,  6,  8,  1,  3,  3,  2,  7,  6,  2,  1,  9,  8,
	18, 6,  3,  7, 14,  1,  6,  3,  6,  3,  2,  1,  8, 15,  4, 12,
	3, 15,  5,  1,  9,  2,  3,  6,  4, 11,  1,  3, 11,  9,  1,  5,
	1,  5, 15,  1, 14,  3,  7,  8,  3, 10,  8,  1,  3,  2, 16,  2,
	1,  2,  3,  1,  6,  2,  3,  3,  6,  1,  3,  2,  3,  4,  3,  2,
	10, 2, 16,  5,  4,  8,  1, 11,  1,  2,  3,  4,  3,  8,  7,  2,
	9,  4,  2, 10,  3,  6,  6,  3,  5,  1,  5,  1,  6, 14,  6,  9,
	1,  9,  5,  4,  5, 24,  1,  2,  3,  4,  5,  1,  5, 15,  1, 18,
	3,  5,  3,  1,  9,  2,  3,  4,  8,  7,  8,  3,  7,  2, 10,  2,
	3,  1,  5,  6,  1,  3,  6,  3,  3,  2,  6,  1,  3,  2,  6,  3,
	4,  2,  1,  3,  9,  5,  3,  4,  6,  3, 11,  1,  3,  6,  9,  2,
	7,  3,  2, 10,  3,  8,  4,  2,  4, 11,  4,  6,  3,  3,  8,  6,
	9, 15,  4,  2,  1,  2,  3, 13,  2,  7, 12, 11,  3,  1,  3,  5,
	3,  7,  3,  3,  6,  5,  3,  1,  6,  5,  6,  4,  9,  9,  5,  3,
	4,  8,  3,  3,  4,  8, 10,  2,  1,  5,  1,  5,  6,  3,  4,  3,
	5, 10,  5,  9, 13,  2,  3, 15,  1,  2,  4,  3,  6,  6,  9,  2,
	4, 11,  3,  1,  6, 17,  3,  9,  6,  3,  1, 14,  7,  8,  7,  2,
	7,  6,  2,  3,  3,  1, 18,  2,  3, 10,  6, 12,  3, 11,  1,  8,
	9,  6,  6,  9,  1,  3,  3,  3,  2,  3,  7,  2,  1, 11,  4,  6,
	3,  5,  3,  4,  6,  9,  6,  3,  5,  1, 11,  7,  3,  3,  2,  9,
	3, 10, 11,  1,  6, 12,  2,  9,  9,  1, 11,  1,  2,  6,  4,  6,
	5,  7,  2,  1,  9,  8, 19,  3,  3,  3,  6,  5,  3,  6,  4,  3,
	2,  3,  7, 15,  3,  5,  4, 11,  3,  4,  6,  5,  1,  5,  1,  3,
	5,  1,  5,  6,  9, 10,  3,  2,  4, 11,  3,  3, 15,  3,  7,  3,
	6,  6,  3,  5,  1,  5, 15,  1,  8,  4,  2,  1,  3,  9,  2,  1,
	3,  2, 13,  2,  4,  3,  5,  1,  2,  3,  4,  2,  3, 15,  6,  1,
	3,  3,  2, 10, 11,  4,  2,  1,  2, 36,  4,  2,  4, 11,  1,  2,
	7,  5,  1,  2, 10,  3,  5,  9,  3, 10,  8,  3,  4,  3,  2, 10,
	6, 11,  1,  2,  1,  6,  5,  9,  1, 11,  3,  9, 15,  1,  5,  7,
	5,  4,  8, 25,  3,  5,  4,  5,  6,  3,  9,  1, 11,  3,  1,  2,
	3,  4,  3,  3,  5,  9,  1, 11,  1,  8,  7,  5,  3,  1,  6,  5,
	10, 2,  7,  3,  2, 18,  1,  2,  3,  6,  1,  2,  7,  6,  3,  2,
	3,  1,  3,  2, 10,  5,  1,  5,  3,  6,  1, 12,  6,  6,  3,  3,
	2, 12,  1,  2, 12,  1,  3,  2,  3,  4,  8,  3,  1,  5,  6,  7,
	3, 17,  3,  7,  3,  2,  1, 15, 11,  4,  2,  3,  4,  2,  1, 14,
	1,  3,  2, 13,  9, 11,  1,  3,  8,  3,  1,  8,  6,  1,  6,  2,
	3,  3,  7,  5,  3,  4,  6,  2,  9,  1,  5,  4,  8,  3,  3, 15,
	1,  5,  9,  1,  5,  4,  2,  4,  6, 12, 20,  1,  6,  5,  3,  6,
	1,  6,  2,  1,  2,  3,  9,  7,  6,  3,  2,  7, 15,  2,  4,  5,
	4,  3,  5,  9,  4,  2,  7,  8,  3,  4,  2,  3,  1,  5,  1,  6,
	2,  1,  2,  3,  4,  2,  3, 16, 12,  5,  4,  9,  5,  1,  3,  5,
	1,  2,  9,  3,  6,  1,  8,  1, 11,  3,  3,  4,  9,  2,  9,  6,
	4,  3,  2, 10,  3, 15, 11,  6,  1,  3,  9,  2, 31,  2,  1,  6,
	3,  5,  1,  6,  6, 14,  1,  2,  7, 11,  3,  1,  3,  3,  5,  7,
	2,  1,  5,  3,  4,  5,  7,  5,  3,  1,  6, 11,  9,  4,  5,  9,
	6,  1,  6,  2,  6,  1,  5,  1,  3,  9,  3,  3, 17,  3,  1,  6,
	2,  3,  9,  9,  1,  8,  3,  3,  4,  3,  5,  9,  4,  5,  4,  5,
	1,  2,  9, 13,  6, 11,  1,  2,  1, 11,  3,  3,  7,  8,  3, 10,
	5,  6,  1,  9, 21,  2, 12,  1,  3,  5,  6,  1,  3,  5,  4,  2,
	3,  6,  6,  4,  2,  3,  6, 15, 10,  3, 12,  3,  5,  6,  1,  5,
	10, 3,  3,  2,  6,  7,  5,  9,  6,  4,  3,  6,  2,  7,  5,  1,
	6, 15,  8,  1,  6,  3,  2,  1,  2,  3, 13,  2,  9,  1,  2,  3,
	7, 27,  3, 26,  1,  8,  3,  3,  6, 13,  2,  1,  3, 11,  3,  1,
	6,  6,  3,  5,  9,  1,  6,  6,  5,  9,  6,  3,  4,  3,  5,  3,
	4,  2,  1,  2, 10, 12,  3,  3,  5,  7,  5,  1, 11,  3,  7,  5,
	13, 2,  9,  4,  6,  6,  5,  6,  3,  4,  8,  3,  4,  3,  3, 11,
	1,  5, 10,  5,  3, 22,  9,  3,  5,  1,  2,  3,  7,  2, 13,  2,
	1,  6,  5,  4,  2,  4,  6,  2,  6,  4, 11,  4,  3,  5,  9,  3,
	3,  4,  3,  6,  2,  4,  9,  5,  6,  3,  6,  1,  3,  2,  1,  8,
	6,  6,  7,  5,  7,  3,  5,  6,  1,  6,  3,  2,  3,  1,  6,  2,
	13, 3,  9,  3,  5,  3,  1,  9,  5,  4,  2, 13,  5, 10,  3,  8,
	10, 6,  5,  4,  5,  1,  8,  3, 10,  5, 10,  2, 15,  1,  2,  4,
	8,  1,  9,  2,  1,  3,  5,  9,  6,  7,  9,  3,  8, 10,  3,  2,
	4,  3,  2,  3,  6,  4,  5,  1,  6,  3,  2,  1,  3,  5,  1,  8,
	6,  7,  5,  3,  4,  3, 14,  1,  3,  9, 15, 17,  1,  8,  6,  1,
	9,  8,  3,  4,  5,  4,  5,  4,  5, 22,  3,  3,  2, 10,  2,  1,
	2,  7, 14,  4,  3,  8,  7, 15,  3, 15,  2,  7,  5,  3,  3,  4,
	2,  9,  6,  3,  1, 11,  6,  4,  3,  6,  2,  7,  2,  3,  1,  2,
	9, 10,  3,  8, 19,  8,  1,  2,  3,  1, 20, 21,  7,  2,  3,  1,
	12, 5,  3,  1,  9,  5,  6,  1,  8,  1,  3,  8,  3,  4,  2,  1,
	5,  3,  4,  5,  1,  9,  8,  4,  6,  9,  6,  3,  6,  5,  3,  3
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
	uint32_t TWO_buf = 2;
	uint8_t r_buf[RSA_MAX_BYTES / 2];
	uint8_t A_buf[RSA_MAX_BYTES / 2];
	uint8_t y_buf[RSA_MAX_BYTES / 2];

	struct LITE_BIGNUM ONE;
	struct LITE_BIGNUM TWO;
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
	DCRYPTO_bn_wrap(&TWO, &TWO_buf, sizeof(TWO_buf));
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
		bn_mont_modexp(&y, &A, &r, p);
		if (bn_eq(&y, &ONE))
			continue;
		bn_add(&y, &ONE);
		if (bn_eq(&y, p))
			continue;
		bn_sub(&y, &ONE);

		/* y = y ^ 2 mod p */
		for (i = 0; i < s - 1; i++) {
			bn_copy(&A, &y);
			bn_mont_modexp(&y, &A, &TWO, p);

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
	for (i = 0; i < sizeof(PRIME_DELTAS) / sizeof(PRIME_DELTAS[0]); i++) {
		uint16_t rem;

		prime += (PRIME_DELTAS[i] << 1);
		rem = bn_mod_word16(p, prime);
		/* Skip marking odd offsets (i.e. even candidates). */
		for (j = (rem == 0) ? 0 : prime - rem;
		     j < bn_bits(&composites) << 1;
		     j += prime) {
			if ((j & 1) == 0)
				bn_set_bit(&composites, j >> 1);
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

	memset(composites_buf, 0, sizeof(composites_buf));
	return 0;
}
