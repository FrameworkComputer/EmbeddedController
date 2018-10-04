/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "math.h"
#include "math_util.h"
#include "vec3.h"
#include "util.h"

void fpv3_scalar_mul(fpv3_t v, fp_t c)
{
	v[X] = fp_mul(v[X], c);
	v[Y] = fp_mul(v[Y], c);
	v[Z] = fp_mul(v[Z], c);
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
