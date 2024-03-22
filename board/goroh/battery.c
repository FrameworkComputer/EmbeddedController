/* Copyright 2021 The ChromiumOS Authors
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
	[BATTERY_LGC_AP18C8K] = {
		.manuf_name = "LGC KT0030G020",
		.config = {
			.fuel_gauge = {
				.ship_mode = {
					.reg_addr = 0x3A,
					.reg_data = { 0xC574, 0xC574 },
				},
				.fet = {
					.reg_addr = 0x43,
					.reg_mask = 0x0001,
					.disconnect_val = 0x0,
					.cfet_mask = 0x0002,
					.cfet_off_val = 0x0000,
				},
			},
			.batt_info = {
				.voltage_max            = 13050,
				.voltage_normal         = 11250,
				.voltage_min            = 9000,
				.precharge_current      = 256,
				.start_charging_min_c   = 0,
				.start_charging_max_c   = 50,
				.charging_min_c         = 0,
				.charging_max_c         = 60,
				.discharging_min_c      = -20,
				.discharging_max_c      = 75,
			},
		},
	},
};
BUILD_ASSERT(ARRAY_SIZE(board_battery_info) == BATTERY_TYPE_COUNT);

const enum battery_type DEFAULT_BATTERY_TYPE = BATTERY_LGC_AP18C8K;
