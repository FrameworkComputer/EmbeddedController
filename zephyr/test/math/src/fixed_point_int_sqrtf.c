/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <ztest.h>

#include "math.h"
#include "math_util.h"

ZTEST_USER(math, int_sqrtf_negative)
{
	zassert_equal(int_sqrtf(-100), 0, NULL);
}

ZTEST_USER(math, int_sqrtf_overflow)
{
	zassert_equal(int_sqrtf(INT64_MAX), INT32_MAX, NULL);
}
