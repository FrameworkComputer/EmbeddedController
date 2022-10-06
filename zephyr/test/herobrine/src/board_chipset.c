/* Copyright 2022 The ChromiumOS Authors.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#include "hooks.h"
#include "board_chipset.h"

static int battery_soc_abs_value = 50;

int battery_state_of_charge_abs(int *percent)
{
	*percent = battery_soc_abs_value;
	return EC_SUCCESS;
}

int charger_get_min_bat_pct_for_power_on(void)
{
	return 2;
}

ZTEST_USER(board_chipset, test_good_battery_normal_boot)
{
	timestamp_t start_time;
	uint64_t time_diff_us;

	battery_soc_abs_value = 50;

	start_time = get_time();
	hook_notify(HOOK_CHIPSET_PRE_INIT);
	time_diff_us = get_time().val - start_time.val;

	zassert_true(time_diff_us < 10, "CHIPSET_PRE_INIT hook delayed", NULL);
}

ZTEST_USER(board_chipset, test_low_battery_normal_boot)
{
	timestamp_t start_time;
	uint64_t time_diff_us;

	battery_soc_abs_value = 1;

	start_time = get_time();
	hook_notify(HOOK_CHIPSET_PRE_INIT);
	time_diff_us = get_time().val - start_time.val;

	zassert_true(time_diff_us < 10, "CHIPSET_PRE_INIT hook delayed", NULL);
}

ZTEST_USER(board_chipset, test_low_battery_delayed_boot)
{
	timestamp_t start_time;
	uint64_t time_diff_us;

	battery_soc_abs_value = 1;
	/* The PD connect event delays the power on sequence */
	hook_notify(HOOK_USB_PD_CONNECT);

	start_time = get_time();
	hook_notify(HOOK_CHIPSET_PRE_INIT);
	time_diff_us = get_time().val - start_time.val;

	zassert_true(time_diff_us > 500000, "CHIPSET_PRE_INIT hook not delayed",
		     NULL);
}

static void test_before(void *data)
{
	ARG_UNUSED(data);
	reset_pp5000_inited();
}

ZTEST_SUITE(board_chipset, NULL, NULL, test_before, NULL, NULL);
