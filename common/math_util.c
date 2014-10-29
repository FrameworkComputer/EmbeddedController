/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Common math functions. */

#include "common.h"
#include "math.h"
#include "math_util.h"
#include "util.h"

/* For cosine lookup table, define the increment and the size of the table. */
#define COSINE_LUT_INCR_DEG	5
#define COSINE_LUT_SIZE		((180 / COSINE_LUT_INCR_DEG) + 1)

#ifdef CONFIG_FPU
/* Lookup table for the value of cosine from 0 degrees to 180 degrees. */
static const float cos_lut[] = {
	 1.00000,  0.99619,  0.98481,  0.96593,  0.93969,
	 0.90631,  0.86603,  0.81915,  0.76604,  0.70711,
	 0.64279,  0.57358,  0.50000,  0.42262,  0.34202,
	 0.25882,  0.17365,  0.08716,  0.00000, -0.08716,
	-0.17365, -0.25882, -0.34202, -0.42262, -0.50000,
	-0.57358, -0.64279, -0.70711, -0.76604, -0.81915,
	-0.86603, -0.90631, -0.93969, -0.96593, -0.98481,
	-0.99619, -1.00000,
};
BUILD_ASSERT(ARRAY_SIZE(cos_lut) == COSINE_LUT_SIZE);


float arc_cos(float x)
{
	int i;

	/* Cap x if out of range. */
	if (x < -1.0)
		x = -1.0;
	else if (x > 1.0)
		x = 1.0;

	/*
	 * Increment through lookup table to find index and then linearly
	 * interpolate for precision.
	 */
	/* TODO(crosbug.com/p/25600): Optimize with binary search. */
	for (i = 0; i < COSINE_LUT_SIZE-1; i++)
		if (x >= cos_lut[i+1])
			return COSINE_LUT_INCR_DEG *
			(i + (cos_lut[i] - x) / (cos_lut[i] - cos_lut[i+1]));

	/*
	 * Shouldn't be possible to get here because inputs are clipped to
	 * [-1, 1] and the cos_lut[] table goes over the same range. If we
	 * are here, throw an assert.
	 */
	ASSERT(0);

	return 0;
}

int vector_magnitude(const vector_3_t v)
{
	return sqrtf(SQ(v[0]) + SQ(v[1]) + SQ(v[2]));
}

float cosine_of_angle_diff(const vector_3_t v1, const vector_3_t v2)
{
	int dotproduct;
	float denominator;

	/*
	 * Angle between two vectors is acos(A dot B / |A|*|B|). To return
	 * cosine of angle between vectors, then don't do acos operation.
	 */

	dotproduct = v1[0] * v2[0] + v1[1] * v2[1] + v1[2] * v2[2];

	denominator = vector_magnitude(v1) * vector_magnitude(v2);

	/* Check for divide by 0 although extremely unlikely. */
	if (ABS(denominator) < 0.01F)
		return 0.0;

	return (float)dotproduct / (denominator);
}
#endif

/*
 * rotate a vector v
 *  - support input v and output res are the same vector
 */
void rotate(const vector_3_t v, const matrix_3x3_t R,
		vector_3_t res)
{
	vector_3_t t;

	/* copy input v to temp vector t */
	t[0] = v[0];
	t[1] = v[1];
	t[2] = v[2];

	/* start rotate */
	res[0] =	t[0] * R[0][0] +
			t[1] * R[1][0] +
			t[2] * R[2][0];
	res[1] =	t[0] * R[0][1] +
			t[1] * R[1][1] +
			t[2] * R[2][1];
	res[2] =	t[0] * R[0][2] +
			t[1] * R[1][2] +
			t[2] * R[2][2];
}

