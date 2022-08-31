/* Copyright 2022 The ChromiumOS Authors.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/ztest.h>

#include "ec_commands.h"
#include "lid_angle.h"
#include "test/drivers/test_mocks.h"
#include "test/drivers/test_state.h"

#define LID_ANGLE_MIN_LARGE_ANGLE 0
#define LID_ANGLE_MAX_LARGE_ANGLE 360

static void lid_angle_after(void *f)
{
	ARG_UNUSED(f);
	/* Reset the wake angle */
	lid_angle_set_wake_angle(180);
	/* Flush the buffer */
	lid_angle_update(LID_ANGLE_UNRELIABLE);
	lid_angle_update(LID_ANGLE_UNRELIABLE);
	lid_angle_update(LID_ANGLE_UNRELIABLE);
	lid_angle_update(LID_ANGLE_UNRELIABLE);
}

ZTEST_SUITE(lid_angle, drivers_predicate_post_main, NULL, NULL, lid_angle_after,
	    NULL);

ZTEST(lid_angle, test_get_set_wake_angle)
{
	lid_angle_set_wake_angle(LID_ANGLE_MIN_LARGE_ANGLE - 1);
	zassert_equal(LID_ANGLE_MIN_LARGE_ANGLE, lid_angle_get_wake_angle(),
		      NULL);

	lid_angle_set_wake_angle(LID_ANGLE_MAX_LARGE_ANGLE + 1);
	zassert_equal(LID_ANGLE_MAX_LARGE_ANGLE, lid_angle_get_wake_angle(),
		      NULL);

	lid_angle_set_wake_angle(
		(LID_ANGLE_MIN_LARGE_ANGLE + LID_ANGLE_MAX_LARGE_ANGLE) / 2);
	zassert_equal((LID_ANGLE_MIN_LARGE_ANGLE + LID_ANGLE_MAX_LARGE_ANGLE) /
			      2,
		      lid_angle_get_wake_angle(), NULL);
}

ZTEST(lid_angle, test_no_wake_min_large_angle)
{
	lid_angle_set_wake_angle(LID_ANGLE_MIN_LARGE_ANGLE);
	lid_angle_update(45);
	lid_angle_update(45);
	lid_angle_update(45);
	lid_angle_update(45);

	zassert_equal(1, lid_angle_peripheral_enable_fake.call_count, NULL);
	zassert_equal(0, lid_angle_peripheral_enable_fake.arg0_val, NULL);
}

ZTEST(lid_angle, test_wake_max_large_angle)
{
	lid_angle_set_wake_angle(LID_ANGLE_MAX_LARGE_ANGLE);
	lid_angle_update(45);
	lid_angle_update(45);
	lid_angle_update(45);
	lid_angle_update(45);

	zassert_equal(1, lid_angle_peripheral_enable_fake.call_count, NULL);
	zassert_equal(1, lid_angle_peripheral_enable_fake.arg0_val, NULL);
}
