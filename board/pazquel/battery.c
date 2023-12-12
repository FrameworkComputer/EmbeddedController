/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Battery pack vendor provided charging profile
 */

#include "battery_fuel_gauge.h"
#include "common.h"

const struct batt_conf_embed board_battery_info[] = {
	/* GanFeng SG20 Battery Information */
	[BATTERY_GANFENG] = {
		.manuf_name = "Ganfeng",
		.device_name = "SG20",
		.config = {
			.fuel_gauge = {
				.ship_mode = {
					.reg_addr = 0x00,
					.reg_data = { 0x0010, 0x0010 },
				},
				.fet = {
					.reg_addr = 0x43,
					.reg_mask = 0x0003,
					.disconnect_val = 0x0000,
				},
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
	},
	/* Pow-Tech SG20QT1C-2S4000-P1P1 Battery Information */
	[BATTERY_POWTECH_SG20QT1C] = {
		.manuf_name = "POW-TECH",
		.device_name = "SG20QT1C",
		.config = {
			.fuel_gauge = {
				.ship_mode = {
					.reg_addr = 0x00,
					.reg_data = { 0x0010, 0x0010 },
				},
				.fet = {
					.reg_addr = 0x54,
					.reg_mask = 0x0006,
					.disconnect_val = 0x0000,
				},
				.flags = FUEL_GAUGE_FLAG_MFGACC,
			},
			.batt_info = {
				.voltage_max        = 8700, /* mV */
				.voltage_normal     = 7600, /* mV */
				.voltage_min        = 6000, /* mV */
				.precharge_current  = 256,  /* mA */
				.start_charging_min_c = 0,
				.start_charging_max_c = 45,
				.charging_min_c     = 0,
				.charging_max_c     = 53,
				.discharging_min_c  = -23,
				.discharging_max_c  = 63,
			},
		},
	},
};
BUILD_ASSERT(ARRAY_SIZE(board_battery_info) == BATTERY_TYPE_COUNT);

const enum battery_type DEFAULT_BATTERY_TYPE = BATTERY_GANFENG;
