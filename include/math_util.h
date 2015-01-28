/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Header file for common math functions. */

#ifndef __CROS_MATH_UTIL_H
#define __CROS_MATH_UTIL_H

/* Fixed-point type */
typedef int32_t fp_t;

/* Number of bits left of decimal point for fixed-point */
#define FP_BITS 16

/* Conversion to/from fixed-point */
#define INT_TO_FP(x) ((fp_t)(x) << FP_BITS)
#define FP_TO_INT(x) ((int32_t)((x) >> FP_BITS))
/* Float to fixed-point, only for compile-time constants and unit tests */
#define FLOAT_TO_FP(x) ((fp_t)((x) * (float)(1<<FP_BITS)))
/* Fixed-point to float, for unit tests */
#define FP_TO_FLOAT(x) ((float)(x) / (float)(1<<FP_BITS))

/*
 * Fixed-point addition and subtraction can be done directly, because they
 * work identically.
 */

/**
 * Multiplication - return (a * b)
 */
static inline fp_t fp_mul(fp_t a, fp_t b)
{
	return (fp_t)(((int64_t)a * b) >> FP_BITS);
}

/**
 * Division - return (a / b)
 */
static inline fp_t fp_div(fp_t a, fp_t b)
{
	return (fp_t)(((int64_t)a << FP_BITS) / b);
}

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
	return (a >= 0 ? a : -a);
}

/*
 * Fixed point matrix
 *
 * Note that constant matrices MUST be initialized using FLOAT_TO_FP()
 * or INT_TO_FP() for all non-zero values.
 */
typedef fp_t matrix_3x3_t[3][3];

/* Integer vector */
typedef int vector_3_t[3];

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
fp_t cosine_of_angle_diff(const vector_3_t v1, const vector_3_t v2);

/**
 * Rotate vector v by rotation matrix R.
 *
 * @param v Vector to be rotated.
 * @param R Rotation matrix.
 * @param res Resultant vector.
 */
void rotate(const vector_3_t v, const matrix_3x3_t R, vector_3_t res);



#endif /* __CROS_MATH_UTIL_H */
