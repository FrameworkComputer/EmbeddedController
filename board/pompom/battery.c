/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Battery pack vendor provided charging profile
 */

#include "battery_fuel_gauge.h"
#include "common.h"

const struct batt_conf_embed board_battery_info[] = {
	[BATTERY_BYD] = {
		.manuf_name = "BYD",
		.config = {
			.fuel_gauge = {
				.ship_mode = {
					.reg_addr = 0x00,
					.reg_data = { 0x0010, 0x0010 },
				},
				.fet = {
					.reg_addr = 0x0,
					.reg_mask = 0x2000,
					.disconnect_val = 0x2000,
				},
			},
			.batt_info = {
				.voltage_max        = 8800, /* mV */
				.voltage_normal     = 7700, /* mV */
				.voltage_min        = 6000, /* mV */
				.precharge_current  = 256,  /* mA */
				.start_charging_min_c = 0,
				.start_charging_max_c = 50,
				.charging_min_c     = 0,
				.charging_max_c     = 60,
				.discharging_min_c  = -20,
				.discharging_max_c  = 70,
			},
		},
	},
};
BUILD_ASSERT(ARRAY_SIZE(board_battery_info) == BATTERY_TYPE_COUNT);

const enum battery_type DEFAULT_BATTERY_TYPE = BATTERY_BYD;
