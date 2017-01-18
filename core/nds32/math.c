/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"

#ifdef CONFIG_FPU
union ieee_float_shape_type {
	float value;
	uint32_t word;
};

/* Get a 32 bit int from a float.  */
#define GET_FLOAT_WORD(i, d) \
	do { \
		union ieee_float_shape_type gf_u; \
		gf_u.value = (d); \
		(i) = gf_u.word; \
	} while (0)

/* Set a float from a 32 bit int. */
#define SET_FLOAT_WORD(d, i) \
	do { \
		union ieee_float_shape_type sf_u; \
		sf_u.word = (i); \
		(d) = sf_u.value; \
	} while (0)

float fabsf(float x)
{
	uint32_t ix;

	GET_FLOAT_WORD(ix, x);
	SET_FLOAT_WORD(x, (ix & 0x7fffffff));

	return x;
}

#define FLT_UWORD_IS_ZERO(x) ((x) == 0)
#define FLT_UWORD_IS_SUBNORMAL(x) ((x) < 0x00800000L)
#define FLT_UWORD_IS_FINITE(x) ((x) < 0x7f800000L)

static const float one = 1.0f, tiny = 1.0e-30f;
static float __ieee754_sqrtf(float x)
{
	float z;
	uint32_t r, hx;
	int32_t ix, s, q, m, t, i;

	GET_FLOAT_WORD(ix, x);
	hx = ix & 0x7fffffff;

	/*
	 * take care of Inf and NaN
	 * sqrt(NaN)=NaN, sqrt(+inf)=+inf, sqrt(-inf)=sNaN
	 */
	if (!FLT_UWORD_IS_FINITE(hx))
		return x * x + x;
	/* take care of zero and -ves */
	if (FLT_UWORD_IS_ZERO(hx))
		return x;
	if (ix < 0)
		return (x - x) / (x - x);

	m = (ix >> 23);
	if (FLT_UWORD_IS_SUBNORMAL(hx)) {
		for (i = 0; (ix & 0x00800000L) == 0; i++)
			ix <<= 1;
		m -= i - 1;
	}

	m -= 127;
	ix = (ix & 0x007fffffL) | 0x00800000L;
	if (m & 1)
		ix += ix;

	m >>= 1;
	ix += ix;
	q = s = 0;
	r = 0x01000000L;

	while (r != 0) {
		t = s + r;
		if (t <= ix) {
			s = t + r;
			ix -= t;
			q += r;
		}
		ix += ix;
		r >>= 1;
	}

	if (ix != 0) {
		z = one - tiny;
		if (z >= one) {
			z = one + tiny;
			if (z > one)
				q += 2;
			else
				q += (q & 1);
		}
	}

	ix = (q >> 1) + 0x3f000000L;
	ix += (m << 23);
	SET_FLOAT_WORD(z, ix);

	return z;
}

float sqrtf(float x)
{
	return __ieee754_sqrtf(x);
}
#endif
