/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Battery pack vendor provided charging profile
 */

#include "battery_fuel_gauge.h"
#include "battery_smart.h"
#include "common.h"
#include "util.h"

const struct board_batt_params board_battery_info[] = {
	/*
	 * Getac Battery (Getac SMP-HHP-408) Information
	 * Fuel gauge: BQ40Z50-R3
	 */
	[BATTERY_GETAC_SMP_HHP_408] = {
		.fuel_gauge = {
			.manuf_name = "Getac",
			.ship_mode = {
				.reg_addr = 0x00,
				.reg_data = { 0x0010, 0x0010 },
			},
			.fet = {
				.reg_addr = 0x0,
				.reg_mask = 0x6000,
				.disconnect_val = 0x6000,
			}
		},
		.batt_info = {
			.voltage_max = 13050,        /* mV */
			.voltage_normal = 11400,
			.voltage_min = 9000,
			.precharge_current = 256,   /* mA */
			.start_charging_min_c = 0,
			.start_charging_max_c = 45,
			.charging_min_c = 0,
			.charging_max_c = 45,
			.discharging_min_c = 0,
			.discharging_max_c = 60,
		},
	},
};
BUILD_ASSERT(ARRAY_SIZE(board_battery_info) == BATTERY_TYPE_COUNT);

const enum battery_type DEFAULT_BATTERY_TYPE = BATTERY_GETAC_SMP_HHP_408;
