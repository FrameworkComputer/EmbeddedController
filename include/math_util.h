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
 * @param R Rotation matrix.
 * @param res Resultant vector.
 */
void rotate(const vector_3_t v, const matrix_3x3_t R,
		vector_3_t res);



#endif /* __CROS_MATH_UTIL_H */
