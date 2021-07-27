/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Battery pack vendor provided charging profile
 */

#include "battery_fuel_gauge.h"
#include "common.h"

const struct board_batt_params board_battery_info[] = {
	[BATTERY_BMSPOW] = {
		.fuel_gauge = {
			.manuf_name = "BMSPow",
			.device_name = "SG20",
			.ship_mode = {
				.reg_addr = 0x00,
				.reg_data = { 0x0010, 0x0010 },
			},
			.fet = {
				.reg_addr = 0x43,
				.reg_mask = 0x0003,
				.disconnect_val = 0x0000,
			}
                  
		},
		.batt_info = {
			.voltage_max        = 8700, /* mV */
			.voltage_normal     = 7600, /* mV */
			.voltage_min        = 6000, /* mV */
			.precharge_current  = 256,  /* mA */
			.start_charging_min_c = 0,
			.start_charging_max_c = 50,
			.charging_min_c     = 0,
			.charging_max_c     = 60,
			.discharging_min_c  = -20,
			.discharging_max_c  = 60,
		},
	},
};
BUILD_ASSERT(ARRAY_SIZE(board_battery_info) == BATTERY_TYPE_COUNT);

const enum battery_type DEFAULT_BATTERY_TYPE = BATTERY_BMSPOW;
