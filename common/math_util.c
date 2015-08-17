/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Common math functions. */

#include "common.h"
#include "math.h"
#include "math_util.h"
#include "util.h"

/* Some useful math functions.  Use with integers only! */
#define SQ(x) ((x) * (x))

/* For cosine lookup table, define the increment and the size of the table. */
#define COSINE_LUT_INCR_DEG	5
#define COSINE_LUT_SIZE		((180 / COSINE_LUT_INCR_DEG) + 1)

/* Lookup table for the value of cosine from 0 degrees to 180 degrees. */
static const fp_t cos_lut[] = {
	FLOAT_TO_FP( 1.00000), FLOAT_TO_FP( 0.99619), FLOAT_TO_FP( 0.98481),
	FLOAT_TO_FP( 0.96593), FLOAT_TO_FP( 0.93969), FLOAT_TO_FP( 0.90631),
	FLOAT_TO_FP( 0.86603), FLOAT_TO_FP( 0.81915), FLOAT_TO_FP( 0.76604),
	FLOAT_TO_FP( 0.70711), FLOAT_TO_FP( 0.64279), FLOAT_TO_FP( 0.57358),
	FLOAT_TO_FP( 0.50000), FLOAT_TO_FP( 0.42262), FLOAT_TO_FP( 0.34202),
	FLOAT_TO_FP( 0.25882), FLOAT_TO_FP( 0.17365), FLOAT_TO_FP( 0.08716),
	FLOAT_TO_FP( 0.00000), FLOAT_TO_FP(-0.08716), FLOAT_TO_FP(-0.17365),
	FLOAT_TO_FP(-0.25882), FLOAT_TO_FP(-0.34202), FLOAT_TO_FP(-0.42262),
	FLOAT_TO_FP(-0.50000), FLOAT_TO_FP(-0.57358), FLOAT_TO_FP(-0.64279),
	FLOAT_TO_FP(-0.70711), FLOAT_TO_FP(-0.76604), FLOAT_TO_FP(-0.81915),
	FLOAT_TO_FP(-0.86603), FLOAT_TO_FP(-0.90631), FLOAT_TO_FP(-0.93969),
	FLOAT_TO_FP(-0.96593), FLOAT_TO_FP(-0.98481), FLOAT_TO_FP(-0.99619),
	FLOAT_TO_FP(-1.00000),
};
BUILD_ASSERT(ARRAY_SIZE(cos_lut) == COSINE_LUT_SIZE);


fp_t arc_cos(fp_t x)
{
	int i;

	/* Cap x if out of range. */
	if (x < FLOAT_TO_FP(-1.0))
		x = FLOAT_TO_FP(-1.0);
	else if (x > FLOAT_TO_FP(1.0))
		x = FLOAT_TO_FP(1.0);

	/*
	 * Increment through lookup table to find index and then linearly
	 * interpolate for precision.
	 */
	/* TODO(crosbug.com/p/25600): Optimize with binary search. */
	for (i = 0; i < COSINE_LUT_SIZE-1; i++) {
		if (x >= cos_lut[i+1]) {
			const fp_t interp = fp_div(cos_lut[i] - x,
						   cos_lut[i] - cos_lut[i + 1]);

			return fp_mul(INT_TO_FP(COSINE_LUT_INCR_DEG),
				      INT_TO_FP(i) + interp);
		}
	}

	/*
	 * Shouldn't be possible to get here because inputs are clipped to
	 * [-1, 1] and the cos_lut[] table goes over the same range. If we
	 * are here, throw an assert.
	 */
	ASSERT(0);

	return 0;
}

/**
 * Integer square root.
 */
int int_sqrtf(int64_t x)
{
	int rmax = 0x7fffffff;
	int rmin = 0;

	/* Short cut if x is 32-bit value */
	if (x < rmax)
		rmax = 0x7fff;

	/*
	 * Just binary-search.  There are better algorithms, but we call this
	 * infrequently enough it doesn't matter.
	 */
	if (x <= 0)
		return 0;  /* Yeah, for imaginary numbers too */
	else if (x > (int64_t)rmax * rmax)
		return rmax;

	while (1) {
		int r = (rmax + rmin) / 2;
		int64_t r2 = (int64_t)r * r;

		if (r2 > x) {
			/* Guessed too high */
			rmax = r;
		} else if (r2 < x) {
			/* Guessed too low.  Watch out for rounding! */
			if (rmin == r)
				return r;

			rmin = r;
		} else {
			/* Bullseye! */
			return r;
		}
	}
}

int vector_magnitude(const vector_3_t v)
{
	int64_t sum =   (int64_t)v[0] * v[0] +
			(int64_t)v[1] * v[1] +
			(int64_t)v[2] * v[2];

	return int_sqrtf(sum);
}

fp_t cosine_of_angle_diff(const vector_3_t v1, const vector_3_t v2)
{
	int64_t dotproduct;
	int64_t denominator;

	/*
	 * Angle between two vectors is acos(A dot B / |A|*|B|). To return
	 * cosine of angle between vectors, then don't do acos operation.
	 */

	dotproduct =	(int64_t)v1[0] * v2[0] +
			(int64_t)v1[1] * v2[1] +
			(int64_t)v1[2] * v2[2];

	denominator = (int64_t)vector_magnitude(v1) * vector_magnitude(v2);

	/* Check for divide by 0 although extremely unlikely. */
	if (!denominator)
		return 0;

	/*
	 * We must shift the dot product first, so that we can represent
	 * fractions.  The answer is always a number with magnitude < 1.0, so
	 * if we don't shift, it will always round down to 0.
	 *
	 * Note that overflow is possible if the dot product is large (that is,
	 * if the vector components are of size (31 - FP_BITS/2) bits.  If that
	 * ever becomes a problem, we could detect this by counting the leading
	 * zeroes of the dot product and shifting the denominator down
	 * partially instead of shifting the dot product up.  With the current
	 * FP_BITS=16, that happens if the vector components are ~2^23.  Which
	 * we're a long way away from; the vector components used in
	 * accelerometer calculations are ~2^11.
	 */
	return (dotproduct << FP_BITS) / denominator;
}

/*
 * rotate a vector v
 *  - support input v and output res are the same vector
 */
void rotate(const vector_3_t v, const matrix_3x3_t R, vector_3_t res)
{
	int64_t t[3];

	if (R == NULL) {
		if (v != res)
			memcpy(res, v, sizeof(*v));
		return;
	}


	/* Rotate */
	t[0] =	(int64_t)v[0] * R[0][0] +
		(int64_t)v[1] * R[1][0] +
		(int64_t)v[2] * R[2][0];
	t[1] =	(int64_t)v[0] * R[0][1] +
		(int64_t)v[1] * R[1][1] +
		(int64_t)v[2] * R[2][1];
	t[2] =	(int64_t)v[0] * R[0][2] +
		(int64_t)v[1] * R[1][2] +
		(int64_t)v[2] * R[2][2];

	/* Scale by fixed point shift when writing back to result */
	res[0] = t[0] >> FP_BITS;
	res[1] = t[1] >> FP_BITS;
	res[2] = t[2] >> FP_BITS;
}

void rotate_inv(const vector_3_t v, const matrix_3x3_t R, vector_3_t res)
{
	int64_t t[3];
	fp_t deter;

	if (R == NULL) {
		if (v != res)
			memcpy(res, v, sizeof(*v));
		return;
	}

	deter = fp_mul(R[0][0], (fp_mul(R[1][1], R[2][2]) -
				 fp_mul(R[2][1], R[1][2]))) -
		fp_mul(R[0][1], (fp_mul(R[1][0], R[2][2]) -
				 fp_mul(R[1][2], R[2][0]))) +
		fp_mul(R[0][2], (fp_mul(R[1][0], R[2][1]) -
				 fp_mul(R[1][1], R[2][0])));

	/*
	 * invert the matrix: from
	 * http://stackoverflow.com/questions/983999/
	 * simple-3x3-matrix-inverse-code-c
	 */
	t[0] =  (int64_t)v[0] * (fp_mul(R[1][1], R[2][2]) -
				 fp_mul(R[2][1], R[1][2])) -
		(int64_t)v[1] * (fp_mul(R[1][0], R[2][2]) -
				 fp_mul(R[1][2], R[2][0])) +
		(int64_t)v[2] * (fp_mul(R[1][0], R[2][1]) -
				 fp_mul(R[2][0], R[1][1]));

	t[1] =	(int64_t)v[0] * (fp_mul(R[0][1], R[2][2]) -
				 fp_mul(R[0][2], R[2][1])) * -1 +
		(int64_t)v[1] * (fp_mul(R[0][0], R[2][2]) -
				 fp_mul(R[0][2], R[2][0])) -
		(int64_t)v[2] * (fp_mul(R[0][0], R[2][1]) -
				 fp_mul(R[2][0], R[0][1]));

	t[2] =	(int64_t)v[0] * (fp_mul(R[0][1], R[1][2]) -
				 fp_mul(R[0][2], R[1][1])) -
		(int64_t)v[1] * (fp_mul(R[0][0], R[1][2]) -
				 fp_mul(R[1][0], R[0][2])) +
		(int64_t)v[2] * (fp_mul(R[0][0], R[1][1]) -
				 fp_mul(R[1][0], R[0][1]));

	/* Scale by fixed point shift when writing back to result */
	res[0] = fp_div(t[0], deter) >> FP_BITS;
	res[1] = fp_div(t[1], deter) >> FP_BITS;
	res[2] = fp_div(t[2], deter) >> FP_BITS;
}
