/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "math.h"
#include "math_util.h"
#include "vec3.h"
#include "util.h"

void vec3_scalar_mul(vec3_t v, float c)
{
	v[X] *= c;
	v[Y] *= c;
	v[Z] *= c;
}

float vec3_dot(const vec3_t v, const vec3_t w)
{
	return v[X] * w[X] + v[Y] * w[Y] + v[Z] * w[Z];
}

float vec3_norm_squared(const vec3_t v)
{
	return vec3_dot(v, v);
}

float vec3_norm(const vec3_t v)
{
	return sqrtf(vec3_norm_squared(v));
}

