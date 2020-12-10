/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "battery.h"
#include "battery_fuel_gauge.h"
#include "gpio.h"

const struct board_batt_params board_battery_info[] = {
	[BATTERY_C235] = {
		.fuel_gauge = {
			.manuf_name = "AS3GWRc3KA",
			.device_name = "C235-41",
			.ship_mode = {
				.reg_addr = 0x0,
				.reg_data = { 0x10, 0x10 },
			},
			.fet = {
				.reg_addr = 0x99,
				.reg_mask = 0x0c,
				.disconnect_val = 0x0c,
			}
		},
		.batt_info = {
			.voltage_max		= 8800,
			.voltage_normal		= 7700,
			.voltage_min		= 6000,
			.precharge_current	= 256,
			.start_charging_min_c	= 0,
			.start_charging_max_c	= 45,
			.charging_min_c		= 0,
			.charging_max_c		= 60,
			.discharging_min_c	= 0,
			.discharging_max_c	= 60,
		},
	},
};
BUILD_ASSERT(ARRAY_SIZE(board_battery_info) == BATTERY_TYPE_COUNT);

const enum battery_type DEFAULT_BATTERY_TYPE = BATTERY_C235;
