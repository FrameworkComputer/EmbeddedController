/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Header file for common math functions. */

#ifndef __CROS_MATH_UTIL_H
#define __CROS_MATH_UTIL_H

typedef float matrix_3x3_t[3][3];
typedef int vector_3_t[3];


/* Some useful math functions. */
#define SQ(x) ((x) * (x))
#define ABS(x) ((x) >= 0 ? (x) : -(x))


/**
 * Find acos(x) in degrees. Argument is clipped to [-1.0, 1.0].
 *
 * @param x
 *
 * @return acos(x) in degrees.
 */
float arc_cos(float x);

/**
 * Find the cosine of the angle between two vectors.
 *
 * @param v1
 * @param v2
 *
 * @return Cosine of the angle between v1 and v2.
 */
float cosine_of_angle_diff(const vector_3_t v1, const vector_3_t v2);

/**
 * Rotate vector v by rotation matrix R.
 *
 * @param v Vector to be rotated.
 * @param R Pointer to rotation matrix.
 * @param res Pointer to the resultant vector.
 */
void rotate(const vector_3_t v, const matrix_3x3_t (* const R),
		vector_3_t *res);


#ifdef CONFIG_ACCEL_CALIBRATE

/**
 * Multiply two 3x3 matrices.
 *
 * @param m1
 * @param m2
 * @param res Pointer to resultant matrix R = a1*a2;
 */
void matrix_multiply(matrix_3x3_t *m1, matrix_3x3_t *m2, matrix_3x3_t *res);

/**
 * Given an input matrix and an output matrix, solve for the rotation
 * matrix to get from the input matrix to the output matrix. Note, that this
 * operation is not guaranteed. In order to successfully calculate the rotation
 * matrix, the input must be linearly independent so that the matrix can be
 * inverted.
 *
 * This function solves the following matrix equation for R:
 * in * R = out
 *
 * If input matrix is invertible the resulting rotation matrix is stored in R.
 *
 * @param in
 * @param out
 * @param R Pointer to resultant matrix.
 *
 * @return EC_SUCCESS if successful
 */
int solve_rotation_matrix(matrix_3x3_t *in, matrix_3x3_t *out, matrix_3x3_t *R);

/**
 * Calculate magnitude of a vector.
 *
 * @param v Vector to be measured.
 *
 * @return Magnitued of vector v.
 */
int vector_magnitude(const vector_3_t v);

#endif


#endif /* __CROS_MATH_UTIL_H */
