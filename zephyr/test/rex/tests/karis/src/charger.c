/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "battery_fuel_gauge.h"
#include "charge_manager.h"

#include <zephyr/fff.h>
#include <zephyr/ztest.h>

extern int battery_fuel_gauge_type_override;

FAKE_VALUE_FUNC(int, board_set_active_charge_port, int);
FAKE_VALUE_FUNC(int, power_button_is_pressed);
FAKE_VOID_FUNC(pd_power_supply_reset, int);
FAKE_VALUE_FUNC(int, pd_check_vconn_swap, int);
FAKE_VALUE_FUNC(int, pd_set_power_supply_ready, int);

ZTEST_SUITE(karis_charger, NULL, NULL, NULL, NULL, NULL);

ZTEST(karis_charger, test_get_leave_safe_mode_delay_ms)
{
	/* cosmx battery should delay 2000ms to leave safe mode. */
	battery_fuel_gauge_type_override =
		BATTERY_TYPE(DT_NODELABEL(default_battery));
	zassert_equal(board_get_leave_safe_mode_delay_ms(), 2000);

	battery_fuel_gauge_type_override = BATTERY_TYPE(DT_NODELABEL(cosmx_3));
	zassert_equal(board_get_leave_safe_mode_delay_ms(), 2000);

	/* Not cosmx battery would use defaut delay time 500ms. */
	battery_fuel_gauge_type_override =
		BATTERY_TYPE(DT_NODELABEL(lges_ap23a8l));
	zassert_equal(board_get_leave_safe_mode_delay_ms(), 500);

	battery_fuel_gauge_type_override =
		BATTERY_TYPE(DT_NODELABEL(panasonic_ap23a5l));
	zassert_equal(board_get_leave_safe_mode_delay_ms(), 500);

	battery_fuel_gauge_type_override =
		BATTERY_TYPE(DT_NODELABEL(lgc_ap19b8m));
	zassert_equal(board_get_leave_safe_mode_delay_ms(), 500);
}
