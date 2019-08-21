/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "dcrypto.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/param.h>

#include <openssl/bn.h>

/**
 * Compatibility layer for OpenSSL 1.0.x.
 * BN_bn2lebinpad and BN_lebin2bn were added in OpenSSL 1.1, to provide
 * import/export functionality as BIGNUM struct became opaque.
 */
#if OPENSSL_VERSION_NUMBER < 0x10100000L
#define BN_RAND_TOP_ANY -1
#define BN_RAND_TOP_ONE 0
#define BN_RAND_TOP_TWO 1
#define BN_RAND_BOTTOM_ODD 1
#define BN_RAND_BOTTOM_ANY 0

/* export BIGNUM as little-endian padded to tolen bytes binary */
static int BN_bn2lebinpad(const BIGNUM *a, unsigned char *to, int tolen)
{
	int i;
	BN_ULONG l;

	bn_check_top(a);
	i = BN_num_bytes(a);
	if (tolen < i)
		return -1;
	/* Add trailing zeroes if necessary */
	if (tolen > i)
		memset(to + i, 0, tolen - i);
	to += i;
	while (i--) {
		l = a->d[i / BN_BYTES];
		to--;
		*to = (unsigned char)(l >> (8 * (i % BN_BYTES))) & 0xff;
	}
	return tolen;
}

/* import BIGNUM from little-endian binary of specified length */
static BIGNUM *BN_lebin2bn(const unsigned char *s, int len, BIGNUM *ret)
{
	unsigned int i, m;
	unsigned int n;
	BN_ULONG l;
	BIGNUM *bn = NULL;

	if (ret == NULL)
		ret = bn = BN_new();
	if (ret == NULL)
		return (NULL);
	bn_check_top(ret);
	s += len;
	/* Skip trailing zeroes. */
	for (; len > 0 && s[-1] == 0; s--, len--)
		continue;
	n = len;
	if (n == 0) {
		ret->top = 0;
		return ret;
	}
	i = ((n - 1) / BN_BYTES) + 1;
	m = ((n - 1) % (BN_BYTES));
	if (bn_wexpand(ret, (int)i) == NULL) {
		BN_free(bn);
		return NULL;
	}
	ret->top = i;
	ret->neg = 0;
	l = 0;
	while (n--) {
		s--;
		l = (l << 8L) | *s;
		if (m-- == 0) {
			ret->d[--i] = l;
			l = 0;
			m = BN_BYTES - 1;
		}
	}
	/*
	 * need to call this due to clear byte at top if avoiding
	 * having the top bit set (-ve number)
	 */
	bn_correct_top(ret);
	return ret;
}
#endif

#define MAX_BN_TEST_SIZE 2048

static char to_hexchar(unsigned char c)
{
	return (c < 10) ? c + '0' : c - 10 + 'A';
}

static void hex_print(FILE *fp, unsigned char *d, int size)
{
	char buf[MAX_BN_TEST_SIZE / 4 + 1];
	int i = 0;

	assert((size * 2) + 1 <= sizeof(buf));
	while (i < size) {
		buf[i * 2] = to_hexchar((d[size - i - 1] >> 4) & 0xF);
		buf[i * 2 + 1] = to_hexchar(d[size - i - 1] & 0xF);
		i++;
	};
	buf[size * 2] = 0;
	fprintf(fp, buf);
}

static void dcrypto_print(FILE *fp, struct LITE_BIGNUM *d, int size)
{
	hex_print(fp, (unsigned char *)d->d, size);
}

static int bn_dcrypto_cmpeq(const BIGNUM *b, struct LITE_BIGNUM *d)
{
	unsigned char buf[MAX_BN_TEST_SIZE / 8];
	int size = BN_num_bytes(b);

	assert(size <= sizeof(buf));
	BN_bn2lebinpad(b, buf, size);
	return memcmp(d->d, buf, size);
}

/* Convert OpenSSL BIGNUM to Dcrypto, assumes caller provides buffer */
static void bn_to_dcrypto(const BIGNUM *b, struct LITE_BIGNUM *d, uint32_t *buf,
			  size_t bufsize)
{
	int bn_size = BN_num_bytes(b);

	assert(bn_size <= bufsize);
	memset(buf, 0, bufsize);
	/**
	 * OpenSSL 1.0 was only working for little-endian architectures (x86)
	 * and had direct access to BIGNUM structure, so DCRYPTO_bn_wrap which
	 * just sets a pointer to user provided buffer as source for
	 * LITE_BIGNUM could be applied to data in BIGNUM as is.
	 * In OpenSSL 1.1 BIGNUM became opaque, so we need to export binary
	 * to get data in little-endian format which used by DCRYPTO_*.
	 */
	BN_bn2lebinpad(b, (unsigned char *)buf, bn_size);
	DCRYPTO_bn_wrap(d, buf, bufsize);
}

static int test_bn_modinv_helper(const BIGNUM *E, BN_CTX *ctx, int mod_top,
				 int mod_bottom)
{
	int i, result = 0;
	BIGNUM *MOD, *r;

	BN_CTX_start(ctx);
	MOD = BN_CTX_get(ctx);
	r = BN_CTX_get(ctx);

	for (i = 0; i < 1000; i++) {
		uint32_t m_buf[MAX_BN_TEST_SIZE / LITE_BN_BITS2];
		uint32_t d_buf[MAX_BN_TEST_SIZE / LITE_BN_BITS2];
		uint32_t e_buf[MAX_BN_TEST_SIZE / LITE_BN_BITS2];
		int has_inverse;
		int test_inverse;

		struct LITE_BIGNUM m;
		struct LITE_BIGNUM e;
		struct LITE_BIGNUM d;

		/* Top bit set, bottom bit clear. */
		BN_rand(MOD, MAX_BN_TEST_SIZE, mod_top, mod_bottom);

		if (BN_mod_inverse(r, E, MOD, ctx))
			has_inverse = 1;
		else
			has_inverse = 0;
		bn_to_dcrypto(MOD, &m, m_buf, sizeof(m_buf));
		bn_to_dcrypto(E, &e, e_buf, sizeof(e_buf));

		bn_init(&d, d_buf, sizeof(d_buf));

		test_inverse = bn_modinv_vartime(&d, &e, &m);

		if (test_inverse != has_inverse) {
			fprintf(stderr,
				"ossl inverse: %d, dcrypto inverse: %d\n",
				has_inverse, test_inverse);
			fprintf(stderr, "d : ");
			BN_print_fp(stderr, r);
			fprintf(stderr, "\n");

			fprintf(stderr, "e : ");
			BN_print_fp(stderr, E);
			fprintf(stderr, "\n");

			fprintf(stderr, "M : ");
			BN_print_fp(stderr, MOD);
			fprintf(stderr, "\n");
			result = 1;
			goto fail;
		}

		if (has_inverse) {
			if (bn_dcrypto_cmpeq(r, &d) != 0) {
				fprintf(stderr,
					"dcrypto bn_modinv_vartime fail\n");
				fprintf(stderr, "d : ");
				BN_print_fp(stderr, r);
				fprintf(stderr, "\n dd: ");
				dcrypto_print(stderr, &d, BN_num_bytes(r));
				fprintf(stderr, "\n");

				fprintf(stderr, "e : ");
				BN_print_fp(stderr, E);
				fprintf(stderr, "\n");

				fprintf(stderr, "M : ");
				BN_print_fp(stderr, MOD);
				fprintf(stderr, "\n");

				result = 1;
				goto fail;
			}
		}
	}
fail:
	BN_CTX_end(ctx);
	return result;
}

static int test_bn_modinv(void)
{
	BN_CTX *ctx;
	BIGNUM *E;
	int result = 1;

	ctx = BN_CTX_new();
	BN_CTX_start(ctx);

	E = BN_CTX_get(ctx);

	BN_rand(E, 1024, BN_RAND_TOP_ONE, BN_RAND_BOTTOM_ODD);
	/* Top bit set, bottom bit clear. */
	if (test_bn_modinv_helper(E, ctx, BN_RAND_TOP_ONE, BN_RAND_BOTTOM_ANY))
		goto fail;

	if (test_bn_modinv_helper(E, ctx, BN_RAND_TOP_TWO, BN_RAND_BOTTOM_ANY))
		goto fail;

	BN_rand(E, 32, BN_RAND_TOP_TWO, BN_RAND_BOTTOM_ODD);
	if (test_bn_modinv_helper(E, ctx, BN_RAND_TOP_ONE, BN_RAND_BOTTOM_ANY))
		goto fail;

	BN_rand(E, 17, BN_RAND_TOP_ANY, BN_RAND_BOTTOM_ODD);
	if (test_bn_modinv_helper(E, ctx, BN_RAND_TOP_ONE, BN_RAND_BOTTOM_ANY))
		goto fail;

	BN_set_word(E, 3);
	if (test_bn_modinv_helper(E, ctx, BN_RAND_TOP_ONE, BN_RAND_BOTTOM_ANY))
		goto fail;

	BN_set_word(E, 65537);
	if (test_bn_modinv_helper(E, ctx, BN_RAND_TOP_ONE, BN_RAND_BOTTOM_ANY))
		goto fail;

	result = 0;
fail:
	BN_CTX_end(ctx);
	BN_CTX_free(ctx);
	return result;
}

/* Build a BIGNUM with following template:
 * 11111111111111110000001111111111111000000000000123455667
 * size - size in bits
 * front_ones        mid_ones_pos,mid_ones,     rand_low
 * front_ones - number of 1 bits in highest position
 * mid_ones_pos - starting position of middle ones
 * mid_ones - number of 1 bits in the middle
 * rand_low - number of random low bits
 */
static BIGNUM *bn_gen(BIGNUM *out, int size, int front_ones, int mid_ones_pos,
		      int mid_ones, int rand_low)
{
	unsigned char n[MAX_BN_TEST_SIZE / 8] = {};

	assert(size <= sizeof(n) * 8);
	assert(front_ones < size);
	assert(mid_ones_pos < (size - front_ones - 1));
	assert(mid_ones < (size - mid_ones_pos - 1));
	assert(rand_low < (size - mid_ones_pos - mid_ones - 1));
	/* generate little-endian representation */
	while (front_ones) {
		n[(size - front_ones) / 8] |= 1 << ((size - front_ones) & 7);
		front_ones--;
	}
	while (mid_ones) {
		n[(mid_ones_pos - mid_ones) / 8] |=
			1 << ((mid_ones_pos - mid_ones) & 7);
		mid_ones--;
	}
	while (rand_low) {
		n[(rand_low - 1) / 8] |= (rand() & 1) << ((rand_low - 1) & 7);
		rand_low--;
	}

	return BN_lebin2bn(n, size / 8, out);
}

static int test_bn_div(void)
{
	const int NSIZE = MAX_BN_TEST_SIZE;
	const int PSIZE = MAX_BN_TEST_SIZE / 2;
	BIGNUM *N, *P, *Q, *R;
	BN_CTX *ctx;
	int result = 0, total = 0, prev = 1;
	int nf, nmps, nms, pf, pmps, pms;
	struct LITE_BIGNUM p;
	struct LITE_BIGNUM q;
	struct LITE_BIGNUM n;
	struct LITE_BIGNUM r;

	uint32_t p_buff[MAX_BN_TEST_SIZE / LITE_BN_BITS2];
	uint32_t q_buff[MAX_BN_TEST_SIZE / LITE_BN_BITS2];
	uint32_t n_buff[MAX_BN_TEST_SIZE / LITE_BN_BITS2];
	uint32_t r_buff[MAX_BN_TEST_SIZE / LITE_BN_BITS2];

	ctx = BN_CTX_new();
	BN_CTX_start(ctx);
	N = BN_CTX_get(ctx);
	P = BN_CTX_get(ctx);
	Q = BN_CTX_get(ctx);
	R = BN_CTX_get(ctx);

	for (nf = 1; nf <= NSIZE / 8; nf++)
	for (nmps = NSIZE / 16; nmps < (NSIZE / 16) + 2; nmps++)
	for (nms = NSIZE / 32; nms < (NSIZE / 32) + 2; nms++) {
		N = bn_gen(N, NSIZE, nf, nmps, nms, (nmps - nms) / 2);
		for (pf = 1; pf <= PSIZE / 4; pf++)
		for (pmps = PSIZE / 16; pmps < (PSIZE / 16) + 2; pmps++)
		for (pms = PSIZE / 32; pms < (PSIZE / 32) + 2; pms++) {
			P = bn_gen(P, PSIZE, pf, pmps, pms, (pmps - pms) / 2);
			total++;
			bn_to_dcrypto(N, &n, n_buff, sizeof(n_buff));
			bn_to_dcrypto(P, &p, p_buff, sizeof(p_buff));
			DCRYPTO_bn_wrap(&q, q_buff, sizeof(q_buff));
			DCRYPTO_bn_wrap(&r, r_buff, sizeof(r_buff));

			BN_div(Q, R, N, P, ctx);
			DCRYPTO_bn_div(&q, &r, &n, &p);

			if ((bn_dcrypto_cmpeq(Q, &q) != 0) ||
			    (bn_dcrypto_cmpeq(R, &r) != 0)) {
				result++;
				if (result > prev) {
					/* print only 1 sample in 100000 */
					prev = result + 100000;
					fprintf(stderr, "N : ");
					BN_print_fp(stderr, N);
					fprintf(stderr, "\n");
					fprintf(stderr, "P : ");
					BN_print_fp(stderr, P);
					fprintf(stderr, "\n");

					fprintf(stderr, "Q : ");
					BN_print_fp(stderr, Q);
					fprintf(stderr, "\nQd: ");
					dcrypto_print(stderr, &q,
						      BN_num_bytes(Q));
					fprintf(stderr, "\n");

					fprintf(stderr, "R : ");
					BN_print_fp(stderr, R);
					fprintf(stderr, "\nRd: ");
					dcrypto_print(stderr, &r,
						      BN_num_bytes(R));
					fprintf(stderr, "\n");
				}
			}
		}
	}
	if (result)
		fprintf(stderr, "DCRYPTO_bn_div: total=%d, failures=%d\n",
			total, result);
	BN_CTX_end(ctx);
	BN_CTX_free(ctx);

	return result;
}

void *always_memset(void *s, int c, size_t n)
{
	memset(s, c, n);
	return s;
}

void watchdog_reload(void)
{
}

int main(void)
{
	assert(test_bn_modinv() == 0);
	assert(test_bn_div() == 0);
	fprintf(stderr, "PASS\n");
	return 0;
}
