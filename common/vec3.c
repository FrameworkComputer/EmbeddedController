/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "math.h"
#include "math_util.h"
#include "vec3.h"
#include "util.h"

void floatv3_scalar_mul(floatv3_t v, float c)
{
	v[X] *= c;
	v[Y] *= c;
	v[Z] *= c;
}

float floatv3_dot(const floatv3_t v, const floatv3_t w)
{
	return v[X] * w[X] + v[Y] * w[Y] + v[Z] * w[Z];
}

float floatv3_norm_squared(const floatv3_t v)
{
	return floatv3_dot(v, v);
}

float floatv3_norm(const floatv3_t v)
{
	return sqrtf(floatv3_norm_squared(v));
}

