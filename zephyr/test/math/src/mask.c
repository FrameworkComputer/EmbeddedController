/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <inttypes.h>
#include <ztest.h>

#include "math.h"
#include "math_util.h"

ZTEST_USER(math, bitmask_uint64)
{
	zassert_equal(bitmask_uint64(-1), 0, NULL);
	zassert_equal(bitmask_uint64(64), 0, NULL);
	zassert_equal(bitmask_uint64(1), UINT64_C(1) << 1, NULL);
	zassert_equal(bitmask_uint64(15), UINT64_C(1) << 15, NULL);
	zassert_equal(bitmask_uint64(35), UINT64_C(1) << 35, NULL);
	zassert_equal(bitmask_uint64(60), UINT64_C(1) << 60, NULL);
}
