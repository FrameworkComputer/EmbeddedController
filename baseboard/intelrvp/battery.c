/* Copyright 2018 The Chromium OS Authors. All rights reserved.
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
	 * Simplo Battery (SMP-HHP-408) Information
	 * Fuel gauge: BQ40Z50
	 */
	[BATTERY_SIMPLO_SMP_HHP_408] = {
		.fuel_gauge = {
			.manuf_name = "SMP-HHP-408",
			.ship_mode = {
				.reg_addr = 0x00,
				.reg_data = { 0x0010, 0x0010 },
			},
			.fet = {
				.reg_addr = SB_BATTERY_STATUS,
				.reg_mask = STATUS_INITIALIZED,
				.disconnect_val = 0x0,
			}
		},
		.batt_info = {
			.voltage_max = 8700,        /* mV */
			.voltage_normal = 7600,
			.voltage_min = 6100,
			.precharge_current = 204,   /* mA */
			.start_charging_min_c = 0,
			.start_charging_max_c = 45,
			.charging_min_c = 0,
			.charging_max_c = 45,
			.discharging_min_c = 0,
			.discharging_max_c = 60,
		},
	},

	/*
	 * Simplo Battery (SMP-CA-445) Information
	 * Fuel gauge: BQ30Z554
	 * TODO: SYSCROS-25972
	 */
	[BATTERY_SIMPLO_SMP_CA_445] = {
		.fuel_gauge = {
			.manuf_name = "SMP-CA-445",
			.ship_mode = {
				.reg_addr = 0x00,
				.reg_data = { 0x0010, 0x0010 },
			},
			.fet = {
				.reg_addr = SB_BATTERY_STATUS,
				.reg_mask = STATUS_INITIALIZED,
				.disconnect_val = 0x0,
			}
		},
		.batt_info = {
			.voltage_max = 8700,		/* mV */
			.voltage_normal = 7600,
			.voltage_min = 6100,
			.precharge_current = 150,	/* mA */
			.start_charging_min_c = 0,
			.start_charging_max_c = 45,
			.charging_min_c = 0,
			.charging_max_c = 45,
			.discharging_min_c = -20,
			.discharging_max_c = 60,
		},
	},
};
BUILD_ASSERT(ARRAY_SIZE(board_battery_info) == BATTERY_TYPE_COUNT);

const enum battery_type DEFAULT_BATTERY_TYPE = BATTERY_SIMPLO_SMP_HHP_408;
