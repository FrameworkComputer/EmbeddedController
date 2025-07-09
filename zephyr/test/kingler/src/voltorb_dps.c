/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "battery_smart.h"
#include "chipset.h"
#include "hooks.h"
#include "usb_pd.h"
#include "usb_pd_dpm_sm.h"

#include <zephyr/fff.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#include <dt-bindings/battery.h>

FAKE_VOID_FUNC(x_ec_interrupt);
FAKE_VOID_FUNC(bmi3xx_interrupt);
FAKE_VALUE_FUNC(int, battery_status, int *);
bool voltorb_is_more_efficient(int curr_mv, int prev_mv, int batt_mv,
			       int batt_mw, int input_mw);

bool mock_battery_status_fullycharged(int *battery_status)
{
	*battery_status = SB_STATUS_FULLY_CHARGED;
	return EC_SUCCESS;
}

bool mock_battery_status_notfullycharged(int *battery_status)
{
	*battery_status = SB_STATUS_DISCHARGING;
	return EC_SUCCESS;
}

ZTEST(voltorb_dps_setting, test_fully_charged_battery)
{
	int curr_mv = 15000;
	int prev_mv = 12000;
	int batt_mv = 11000;
	int batt_mw = 45000;
	int input_mw = 65000;

	battery_status_fake.custom_fake = mock_battery_status_fullycharged;
	zassert_true(voltorb_is_more_efficient(curr_mv, prev_mv, batt_mv,
					       batt_mw, input_mw),
		     "Expected 15V to be more efficient when fully charged");
}

ZTEST(voltorb_dps_setting, test_unfilled_battery)
{
	int curr_mv = 13400;
	int prev_mv = 14000;
	int batt_mv = 13500;
	int batt_mw = 45000;
	int input_mw = 65000;

	battery_status_fake.custom_fake = mock_battery_status_fullycharged;
	zassert_true(
		voltorb_is_more_efficient(curr_mv, prev_mv, batt_mv, batt_mw,
					  input_mw),
		"Expected 14V to be more efficient as it is closer to batt_mv");
}

ZTEST(voltorb_dps_setting, test_equal_voltage_diff)
{
	int curr_mv = 14000;
	int prev_mv = 14000;
	int batt_mv = 13500;
	int batt_mw = 45000;
	int input_mw = 65000;

	battery_status_fake.custom_fake = mock_battery_status_notfullycharged;
	zassert_false(
		voltorb_is_more_efficient(curr_mv, prev_mv, batt_mv, batt_mw,
					  input_mw),
		"Expected false when both voltages are equally efficient");
}
ZTEST_SUITE(voltorb_dps_setting, NULL, NULL, NULL, NULL, NULL);
