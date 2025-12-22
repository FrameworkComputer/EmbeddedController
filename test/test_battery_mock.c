/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Mock battery info for tests.
 */

#include "battery.h"
#include "test_util.h"

static const struct battery_info mock_battery_info = {
	.voltage_max = 8400,
	.voltage_normal = 7400,
	.voltage_min = 6000,
	.precharge_current = 256,
	.start_charging_min_c = 0,
	.start_charging_max_c = 45,
	.charging_min_c = 0,
	.charging_max_c = 45,
	.discharging_min_c = -20,
	.discharging_max_c = 60,
};

__attribute__((weak)) const struct battery_info *battery_get_info(void)
{
	return &mock_battery_info;
}
