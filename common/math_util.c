/* Copyright 2014 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Common math functions. */

#include "common.h"
#include "math.h"
#include "math_util.h"
#include "util.h"

/* For cosine lookup table, define the increment and the size of the table. */
#define COSINE_LUT_INCR_DEG 5
#define COSINE_LUT_SIZE ((180 / COSINE_LUT_INCR_DEG) + 1)

/* Lookup table for the value of cosine from 0 degrees to 180 degrees. */
static const fp_t cos_lut[] = {
	FLOAT_TO_FP(1.00000),  FLOAT_TO_FP(0.99619),  FLOAT_TO_FP(0.98481),
	FLOAT_TO_FP(0.96593),  FLOAT_TO_FP(0.93969),  FLOAT_TO_FP(0.90631),
	FLOAT_TO_FP(0.86603),  FLOAT_TO_FP(0.81915),  FLOAT_TO_FP(0.76604),
	FLOAT_TO_FP(0.70711),  FLOAT_TO_FP(0.64279),  FLOAT_TO_FP(0.57358),
	FLOAT_TO_FP(0.50000),  FLOAT_TO_FP(0.42262),  FLOAT_TO_FP(0.34202),
	FLOAT_TO_FP(0.25882),  FLOAT_TO_FP(0.17365),  FLOAT_TO_FP(0.08716),
	FLOAT_TO_FP(0.00000),  FLOAT_TO_FP(-0.08716), FLOAT_TO_FP(-0.17365),
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
	for (i = 0; i < COSINE_LUT_SIZE - 1; i++) {
		if (x >= cos_lut[i + 1]) {
			const fp_t interp = fp_div(cos_lut[i] - x,
						   cos_lut[i] - cos_lut[i + 1]);

			return fp_mul(INT_TO_FP(COSINE_LUT_INCR_DEG),
				      INT_TO_FP(i) + interp);
		}
	}

	/*
	 * Shouldn't be possible to get here because inputs are clipped to
	 * [-1, 1] and the cos_lut[] table goes over the same range.
	 */
	__builtin_unreachable(); /* LCOV_EXCL_LINE */
}

/**
 * Integer square root.
 */
#ifdef CONFIG_FPU
/*
 * Use library sqrtf instruction, if available, since it's usually much faster
 * and smaller. On Cortex-M4, this becomes a single instruction which takes
 * 14 cycles to execute. This produces identical results to binary search,
 * except when the floating point representation of the square root rounds up
 * to an integer.
 */
inline int int_sqrtf(fp_inter_t x)
{
	return sqrtf(x);
}

/* If the platform support FPU, just return sqrtf. */
fp_t fp_sqrtf(fp_t x)
{
	return sqrtf(x);
}
#else
int int_sqrtf(fp_inter_t x)
{
	int rmax = INT32_MAX;
	int rmin = 0;

	/*
	 * Short cut if x is 32-bit value
	 * sqrt(INT32_MAX) ~= 46340.95
	 */
	if (x < INT32_MAX)
		rmax = 46341;

	/*
	 * Just binary-search.  There are better algorithms, but we call this
	 * infrequently enough it doesn't matter.
	 */
	if (x <= 0)
		return 0; /* Yeah, for imaginary numbers too */
	else if (x >= (fp_inter_t)rmax * rmax)
		return rmax;

	while (1) {
		int r = rmin + (rmax - rmin) / 2;
		fp_inter_t r2 = (fp_inter_t)r * r;

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

fp_t fp_sqrtf(fp_t x)
{
	fp_inter_t preshift_x = (fp_inter_t)x << FP_BITS;

	return int_sqrtf(preshift_x);
}
#endif /* CONFIG_FPU */

int vector_magnitude(const intv3_t v)
{
	fp_inter_t sum = (fp_inter_t)v[0] * v[0] + (fp_inter_t)v[1] * v[1] +
			 (fp_inter_t)v[2] * v[2];

	return int_sqrtf(sum);
}

/* cross_product only works if the vectors magnitudes are around 1<<16. */
void cross_product(const intv3_t v1, const intv3_t v2, intv3_t v)
{
	v[X] = (fp_inter_t)v1[Y] * v2[Z] - (fp_inter_t)v1[Z] * v2[Y];
	v[Y] = (fp_inter_t)v1[Z] * v2[X] - (fp_inter_t)v1[X] * v2[Z];
	v[Z] = (fp_inter_t)v1[X] * v2[Y] - (fp_inter_t)v1[Y] * v2[X];
}

fp_inter_t dot_product(const intv3_t v1, const intv3_t v2)
{
	return (fp_inter_t)v1[X] * v2[X] + (fp_inter_t)v1[Y] * v2[Y] +
	       (fp_inter_t)v1[Z] * v2[Z];
}

void vector_scale(intv3_t v, fp_t s)
{
	v[X] = fp_mul(v[X], s);
	v[Y] = fp_mul(v[Y], s);
	v[Z] = fp_mul(v[Z], s);
}

fp_t cosine_of_angle_diff(const intv3_t v1, const intv3_t v2)
{
	fp_inter_t dotproduct;
	fp_inter_t denominator;

	/*
	 * Angle between two vectors is acos(A dot B / |A|*|B|). To return
	 * cosine of angle between vectors, then don't do acos operation.
	 */
	dotproduct = dot_product(v1, v2);

	denominator = (fp_inter_t)vector_magnitude(v1) * vector_magnitude(v2);

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
	return fp_div(dotproduct, denominator);
}

/*
 * rotate a vector v
 *  - support input v and output res are the same vector
 */
void rotate(const intv3_t v, const mat33_fp_t R, intv3_t res)
{
	fp_inter_t t[3];

	if (R == NULL) {
		if (v != res)
			memcpy(res, v, sizeof(intv3_t));
		return;
	}

	/* Rotate */
	t[0] = (fp_inter_t)v[0] * R[0][0] + (fp_inter_t)v[1] * R[1][0] +
	       (fp_inter_t)v[2] * R[2][0];
	t[1] = (fp_inter_t)v[0] * R[0][1] + (fp_inter_t)v[1] * R[1][1] +
	       (fp_inter_t)v[2] * R[2][1];
	t[2] = (fp_inter_t)v[0] * R[0][2] + (fp_inter_t)v[1] * R[1][2] +
	       (fp_inter_t)v[2] * R[2][2];

	/* Scale by fixed point shift when writing back to result */
	res[0] = FP_TO_INT(t[0]);
	res[1] = FP_TO_INT(t[1]);
	res[2] = FP_TO_INT(t[2]);
}

void rotate_inv(const intv3_t v, const mat33_fp_t R, intv3_t res)
{
	fp_inter_t t[3];
	fp_t deter;

	if (R == NULL) {
		if (v != res)
			memcpy(res, v, sizeof(intv3_t));
		return;
	}

	deter = fp_mul(R[0][0],
		       (fp_mul(R[1][1], R[2][2]) - fp_mul(R[2][1], R[1][2]))) -
		fp_mul(R[0][1],
		       (fp_mul(R[1][0], R[2][2]) - fp_mul(R[1][2], R[2][0]))) +
		fp_mul(R[0][2],
		       (fp_mul(R[1][0], R[2][1]) - fp_mul(R[1][1], R[2][0])));

	/*
	 * invert the matrix: from
	 * http://stackoverflow.com/questions/983999/
	 * simple-3x3-matrix-inverse-code-c
	 */
	t[0] = (fp_inter_t)v[0] *
		       (fp_mul(R[1][1], R[2][2]) - fp_mul(R[2][1], R[1][2])) -
	       (fp_inter_t)v[1] *
		       (fp_mul(R[1][0], R[2][2]) - fp_mul(R[1][2], R[2][0])) +
	       (fp_inter_t)v[2] *
		       (fp_mul(R[1][0], R[2][1]) - fp_mul(R[2][0], R[1][1]));

	t[1] = (fp_inter_t)v[0] *
		       (fp_mul(R[0][1], R[2][2]) - fp_mul(R[0][2], R[2][1])) *
		       -1 +
	       (fp_inter_t)v[1] *
		       (fp_mul(R[0][0], R[2][2]) - fp_mul(R[0][2], R[2][0])) -
	       (fp_inter_t)v[2] *
		       (fp_mul(R[0][0], R[2][1]) - fp_mul(R[2][0], R[0][1]));

	t[2] = (fp_inter_t)v[0] *
		       (fp_mul(R[0][1], R[1][2]) - fp_mul(R[0][2], R[1][1])) -
	       (fp_inter_t)v[1] *
		       (fp_mul(R[0][0], R[1][2]) - fp_mul(R[1][0], R[0][2])) +
	       (fp_inter_t)v[2] *
		       (fp_mul(R[0][0], R[1][1]) - fp_mul(R[1][0], R[0][1]));

	/* Scale by fixed point shift when writing back to result */
	res[0] = FP_TO_INT(fp_div(t[0], deter));
	res[1] = FP_TO_INT(fp_div(t[1], deter));
	res[2] = FP_TO_INT(fp_div(t[2], deter));
}

/* division that round to the nearest integer */
int round_divide(int64_t dividend, int divisor)
{
	return (dividend > 0) ^ (divisor > 0) ?
		       (dividend - divisor / 2) / divisor :
		       (dividend + divisor / 2) / divisor;
}

#if ULONG_MAX == 0xFFFFFFFFUL
/*
 * 32 bit processor
 *
 * Some 32 bit processors do not include a 64 bit shift
 * operation. So fall back to 32 bit operations on a
 * union.
 */
uint64_t bitmask_uint64(int offset)
{
	union mask64_t {
		struct {
#if (__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)
			uint32_t lo;
			uint32_t hi;
#elif (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
			uint32_t hi;
			uint32_t lo;
#endif
		};
		uint64_t val;
	} mask = { 0 };

	/*
	 * If the shift is out of range the result should
	 * remain 0, otherwise perform the shift
	 */
	if (offset >= 0 && offset < 64) {
		if (offset < 32)
			mask.lo = BIT(offset);
		else
			mask.hi = BIT(offset - 32);
	}
	return mask.val;
}
#endif
