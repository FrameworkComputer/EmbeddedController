/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <ztest.h>

#include "math.h"
#include "math_util.h"

ZTEST_USER(math, cosine_of_angle_diff__zero_magnitude_vector)
{
	intv3_t v0 = { 0, 0, 0 };
	intv3_t v1 = { 1, 1, 1 };

	zassert_equal(cosine_of_angle_diff(v0, v1), 0, NULL);
	zassert_equal(cosine_of_angle_diff(v1, v0), 0, NULL);
}

ZTEST_USER(math, rotate_inv__null_matrix)
{
	intv3_t v = { 1, 2, 3 };
	intv3_t r = { 4, 5, 6 };

	rotate_inv(v, NULL, r);
	zassert_equal(v[0], r[0], NULL);
	zassert_equal(v[1], r[1], NULL);
	zassert_equal(v[2], r[2], NULL);
}
