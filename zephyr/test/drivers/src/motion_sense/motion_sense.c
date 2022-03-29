/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <ztest.h>

#include "motion_sense.h"
#include "test/drivers/test_state.h"

ZTEST_SUITE(motion_sense, drivers_predicate_post_main, NULL, NULL, NULL, NULL);

ZTEST_USER(motion_sense, ec_motion_sensor_fill_values)
{
	struct ec_response_motion_sensor_data dst = {
		.data = { 1, 2, 3 },
	};
	const int32_t v[] = { 4, 5, 6 };

	ec_motion_sensor_fill_values(&dst, v);
	zassert_equal(dst.data[0], v[0], NULL);
	zassert_equal(dst.data[1], v[1], NULL);
	zassert_equal(dst.data[2], v[2], NULL);
}

ZTEST_USER(motion_sense, ec_motion_sensor_clamp_i16)
{
	zassert_equal(ec_motion_sensor_clamp_i16(0), 0, NULL);
	zassert_equal(ec_motion_sensor_clamp_i16(200), 200, NULL);
	zassert_equal(ec_motion_sensor_clamp_i16(-512), -512, NULL);
	zassert_equal(ec_motion_sensor_clamp_i16(INT16_MAX + 1), INT16_MAX,
		      NULL);
	zassert_equal(ec_motion_sensor_clamp_i16(INT16_MIN - 1), INT16_MIN,
		      NULL);
}
