/* Copyright 2015 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Header file for common math functions. */
#ifndef __CROS_EC_VEC_3_H
#define __CROS_EC_VEC_3_H

#include "math_util.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef float floatv3_t[3];
typedef fp_t fpv3_t[3];

/**
 * Initialized a vector to all 0.0f.
 *
 * @param v Pointer to the vector that will be initialized.
 */
void fpv3_zero(fpv3_t v);

/**
 * Initialize a vector.
 *
 * @param v Pointer to the vector that will be initialized.
 * @param x The value to use for the X component of v.
 * @param y The value to use for the Y component of v.
 * @param z The value to use for the Z component of v.
 */
void fpv3_init(fpv3_t v, fp_t x, fp_t y, fp_t z);

/**
 * Multiply components of the vector by a scalar.
 *
 * @param v Pointer to the vector that is modified.
 * @param c Scalar value to multiply v by.
 */
void fpv3_scalar_mul(fpv3_t v, fp_t c);

/**
 * Subtract b from a and save the result.
 *
 * @param out Pointer to the vector that will be written to.
 * @param a Pointer to the vector that is being subtracted from.
 * @param b Pointer to the vector that is being subtracted.
 */
void fpv3_sub(fpv3_t out, const fpv3_t a, const fpv3_t b);

/**
 * Adds a and b then save the result.
 *
 * @param out Pointer to the vector that will be written to.
 * @param a Pointer to the first vector being added.
 * @param b Pointer to the second vector being added.
 */
void fpv3_add(fpv3_t out, const fpv3_t a, const fpv3_t b);

/**
 * Perform the dot product of two vectors.
 *
 * @param v Pointer to the first vector.
 * @param w Pointer to the second vector.
 * @return The dot product of v and w.
 */
fp_t fpv3_dot(const fpv3_t v, const fpv3_t w);

/**
 * Compute the length^2 of a vector.
 *
 * @param v Pointer to the vector in question.
 * @return The length^2 of the vector.
 */
fp_t fpv3_norm_squared(const fpv3_t v);

/**
 * Compute the length of a vector.
 *
 * @param v Pointer to the vector in question.
 * @return The length of the vector.
 */
fp_t fpv3_norm(const fpv3_t v);

#ifdef __cplusplus
}
#endif

#endif /* __CROS_EC_VEC_3_H */
