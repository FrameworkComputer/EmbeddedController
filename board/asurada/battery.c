/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "battery.h"
#include "battery_fuel_gauge.h"
#include "battery_smart.h"
#include "charge_manager.h"
#include "chipset.h"
#include "gpio.h"
#include "hooks.h"
#include "system.h"
#include "usb_pd.h"

const struct batt_conf_embed board_battery_info[] = {
	[BATTERY_C235] = {
		.manuf_name = "AS3GWRc3KA",
		.device_name = "C235-41",
		.config = {
			.fuel_gauge = {
				.ship_mode = {
					.reg_addr = 0x0,
					.reg_data = { 0x10, 0x10 },
				},
				.fet = {
					.reg_addr = 0x99,
					.reg_mask = 0x0c,
					.disconnect_val = 0x0c,
				},
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
	},
};
BUILD_ASSERT(ARRAY_SIZE(board_battery_info) == BATTERY_TYPE_COUNT);

const enum battery_type DEFAULT_BATTERY_TYPE = BATTERY_C235;
