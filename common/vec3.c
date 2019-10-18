/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "math.h"
#include "math_util.h"
#include "vec3.h"
#include "util.h"

static fpv3_t zero_initialized_vector = {
	FLOAT_TO_FP(0.0f), FLOAT_TO_FP(0.0f), FLOAT_TO_FP(0.0f)
};

void fpv3_zero(fpv3_t v)
{
	memcpy(v, zero_initialized_vector, sizeof(fpv3_t));
}

void fpv3_init(fpv3_t v, fp_t x, fp_t y, fp_t z)
{
	v[X] = x;
	v[Y] = y;
	v[Z] = z;
}

void fpv3_scalar_mul(fpv3_t v, fp_t c)
{
	v[X] = fp_mul(v[X], c);
	v[Y] = fp_mul(v[Y], c);
	v[Z] = fp_mul(v[Z], c);
}

void fpv3_sub(fpv3_t out, const fpv3_t a, const fpv3_t b)
{
	out[X] = a[X] - b[X];
	out[Y] = a[Y] - b[Y];
	out[Z] = a[Z] - b[Z];
}

void fpv3_add(fpv3_t out, const fpv3_t a, const fpv3_t b)
{
	out[X] = a[X] + b[X];
	out[Y] = a[Y] + b[Y];
	out[Z] = a[Z] + b[Z];
}

fp_t fpv3_dot(const fpv3_t v, const fpv3_t w)
{
	return fp_mul(v[X], w[X]) + fp_mul(v[Y], w[Y]) + fp_mul(v[Z], w[Z]);
}

fp_t fpv3_norm_squared(const fpv3_t v)
{
	return fpv3_dot(v, v);
}

fp_t fpv3_norm(const fpv3_t v)
{
	return fp_sqrtf(fpv3_norm_squared(v));
}
