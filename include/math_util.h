/* Copyright 2014 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Header file for common math functions. */

#ifndef __CROS_EC_MATH_UTIL_H
#define __CROS_EC_MATH_UTIL_H

#include "config.h"
#include "limits.h"

#include <stdint.h>

#ifdef CONFIG_FPU
typedef float fp_t;
typedef float fp_inter_t;

/* Conversion to/from fixed-point */
#define INT_TO_FP(x) ((float)(x))
#define FP_TO_INT(x) ((int32_t)(x))
/* Float to fixed-point, only for compile-time constants and unit tests */
#define FLOAT_TO_FP(x) ((float)(x))
/* Fixed-point to float, for unit tests */
#define FP_TO_FLOAT(x) ((float)(x))

#ifndef CONFIG_ZEPHYR
#define FLT_MAX (3.4028234664e+38)
#define FLT_MIN (1.1754943508e-38)
#endif

#else
/* Fixed-point type */
typedef int32_t fp_t;

/* Type used during fp operation */
typedef int64_t fp_inter_t;

/* Number of bits left of decimal point for fixed-point */
#define FP_BITS 16

/* Conversion to/from fixed-point */
#define INT_TO_FP(x) ((fp_t)(x) << FP_BITS)
#define FP_TO_INT(x) ((int32_t)((x) >> FP_BITS))
/* Float to fixed-point, only for compile-time constants and unit tests */
#define FLOAT_TO_FP(x) ((fp_t)((x) * (float)(1 << FP_BITS)))
/* Fixed-point to float, for unit tests */
#define FP_TO_FLOAT(x) ((float)(x) / (float)(1 << FP_BITS))

#ifndef FLT_MAX
#define FLT_MAX INT32_MAX
#endif
#ifndef FLT_MIN
#define FLT_MIN INT32_MIN
#endif

#endif

/* Some useful math functions.  Use with integers only! */
#define POW2(x) ((x) * (x))

/*
 * Fixed-point addition and subtraction can be done directly, because they
 * work identically.
 */

#ifdef CONFIG_FPU
static inline fp_t fp_mul(fp_t a, fp_t b)
{
	return a * b;
}

static inline fp_t fp_div(fp_t a, fp_t b)
{
	return a / b;
}

/* Don't handle divided-by-zero with FPU, since this should be rare. */
static inline fp_t fp_div_dbz(fp_t a, fp_t b)
{
	return fp_div(a, b);
}
#else
/**
 * Multiplication - return (a * b)
 */
static inline fp_t fp_mul(fp_t a, fp_t b)
{
	return (fp_t)(((fp_inter_t)a * b) >> FP_BITS);
}

/**
 * Division - return (a / b)
 */
static inline fp_t fp_div(fp_t a, fp_t b)
{
	return (fp_t)(((fp_inter_t)a << FP_BITS) / b);
}

/**
 * Division which handles division-by-zero - returns (a / b) if b != 0,
 * INT32_MAX if b == 0.
 */
static inline fp_t fp_div_dbz(fp_t a, fp_t b)
{
	/*
	 * Fixed-point numbers has limited value range.  It is very easy to
	 * be trapped in a divided-by-zero error especially when doing
	 * magnetometer calculation.  We only use fixed-point operations for
	 * motion sensors now, so the precision and correctness for these
	 * operations is not the most important point to consider.  Here
	 * we just let divided-by-zero result becomes INT32_MAX, to prevent
	 * the system failure.
	 */
	return b == FLOAT_TO_FP(0) ? INT32_MAX : fp_div(a, b);
}
#endif

/**
 * Square (a * a)
 */
static inline fp_t fp_sq(fp_t a)
{
	return fp_mul(a, a);
}

/**
 * Absolute value
 */
static inline fp_t fp_abs(fp_t a)
{
	return (a >= INT_TO_FP(0) ? a : -a);
}

/*
 * Return the smallest positive X where M * X >= N.
 *
 * For example, if n = 88 and m = 9, then it returns 10
 * (i.e. 9 * 10 >= 88).
 */
static inline int ceil_for(int n, int m)
{
	return (((n - 1) / m) + 1);
}

/**
 * Integer square root
 */
int int_sqrtf(fp_inter_t x);

/**
 * Square root
 */
fp_t fp_sqrtf(fp_t a);

/*
 * Fixed point matrix
 *
 * Note that constant matrices MUST be initialized using FLOAT_TO_FP()
 * or INT_TO_FP() for all non-zero values.
 */
typedef fp_t mat33_fp_t[3][3];

/* Integer vector */
typedef int intv3_t[3];

/* For vectors, define which coordinates are in which location. */
enum { X, Y, Z, W };
/*
 * Return absolute value of x.  Note that as a macro expansion, this may have
 * side effects if x includes function calls, which is why inline functions
 * like fp_abs() are preferred.
 */
#define ABS(x) ((x) >= 0 ? (x) : -(x))

/**
 * Find acos(x) in degrees. Argument is clipped to [-1.0, 1.0].
 *
 * @param x
 *
 * @return acos(x) in degrees.
 */
fp_t arc_cos(fp_t x);

/**
 * Calculate the dot product of 2 vectors.
 */
fp_inter_t dot_product(const intv3_t v1, const intv3_t v2);

/*
 * Calculate the dot product of 2 vectors,
 *
 * Assume the result vector components fits in 32bit.
 */
void cross_product(const intv3_t v1, const intv3_t v2, intv3_t v);

/**
 * Scale a vector by fixed point constant.
 */
void vector_scale(intv3_t v, fp_t s);

/**
 * Find the cosine of the angle between two vectors.
 *
 * The implementation assumes no vector component is greater than
 * 2^(31 - FP_BITS/2).  For example, 2^23, for FP_BITS=16.
 *
 * @param v1
 * @param v2
 *
 * @return Cosine of the angle between v1 and v2.
 */
fp_t cosine_of_angle_diff(const intv3_t v1, const intv3_t v2);

/**
 * Rotate vector v by rotation matrix R.
 *
 * @param v Vector to be rotated.
 * @param R Rotation matrix.
 * @param res Resultant vector.
 */
void rotate(const intv3_t v, const mat33_fp_t R, intv3_t res);

/**
 * Rotate vector v by rotation matrix R^-1.
 *
 * @param v Vector to be rotated.
 * @param R Rotation matrix.
 * @param res Resultant vector.
 */
void rotate_inv(const intv3_t v, const mat33_fp_t R, intv3_t res);

/**
 * Divide dividend by divisor and round it to the nearest integer.
 */
int round_divide(int64_t dividend, int divisor);

/**
 * Create a 64 bit bitmask of 2^offset
 */
#if ULONG_MAX == 0xFFFFFFFFUL
uint64_t bitmask_uint64(int offset);
#else
#define bitmask_uint64(o) ((uint64_t)1 << (o))
#endif

#endif /* __CROS_EC_MATH_UTIL_H */
