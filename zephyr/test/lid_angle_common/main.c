/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "board.h"

#include <zephyr/fff.h>
#include <zephyr/ztest.h>

DEFINE_FFF_GLOBALS;

static void lid_angle_common_before(void *f)
{
	RESET_FAKE(chipset_in_state);
	RESET_FAKE(keyboard_scan_enable);
	RESET_FAKE(tablet_get_mode);
	FFF_RESET_HISTORY();
}

ZTEST_SUITE(lid_angle_common, NULL, NULL, lid_angle_common_before, NULL, NULL);

ZTEST(lid_angle_common, test_enable)
{
	lid_angle_peripheral_enable(1);
	zassert_equal(1, keyboard_scan_enable_fake.call_count);
	zassert_equal(1, keyboard_scan_enable_fake.arg0_val);
	zassert_equal(KB_SCAN_DISABLE_LID_ANGLE,
		      keyboard_scan_enable_fake.arg1_val);
}

ZTEST(lid_angle_common, test_disable)
{
	lid_angle_peripheral_enable(0);
	zassert_equal(1, keyboard_scan_enable_fake.call_count);
	zassert_equal(0, keyboard_scan_enable_fake.arg0_val);
	zassert_equal(KB_SCAN_DISABLE_LID_ANGLE,
		      keyboard_scan_enable_fake.arg1_val);
}

ZTEST(lid_angle_common, test_disable_in_s0)
{
	chipset_in_state_fake.return_val = 1;

	lid_angle_peripheral_enable(0);
	zassert_equal(0, keyboard_scan_enable_fake.call_count);
}

ZTEST(lid_angle_common, test_override_enable_in_tablet_mode)
{
	Z_TEST_SKIP_IFNDEF(CONFIG_TABLET_MODE);

	tablet_get_mode_fake.return_val = 1;

	lid_angle_peripheral_enable(1);
	zassert_equal(1, keyboard_scan_enable_fake.call_count);
	zassert_equal(0, keyboard_scan_enable_fake.arg0_val);
	zassert_equal(KB_SCAN_DISABLE_LID_ANGLE,
		      keyboard_scan_enable_fake.arg1_val);
}
