/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Battery pack information
 */

#include "battery.h"
#include "battery_fuel_gauge.h"
#include "common.h"
#include "ec_commands.h"
#include "extpower.h"

/* Shutdown mode parameter to write to manufacturer access register */
#define SB_SHUTDOWN_DATA 0x0010

const struct board_batt_params board_battery_info[] = {
	[BATTERY_SIMPLO_HIGHPOWER] = {
		.fuel_gauge = {
			.manuf_name = "333-1D-DA-A",
			.ship_mode = {
				.reg_addr = 0x00,
				.reg_data = { 0x0010, 0x0010 },
			},
			.fet = {
				.reg_addr = 0x0,
				.reg_mask = 0x6000,
				.disconnect_val = 0x6000,
			},
			.flags = FUEL_GAUGE_FLAG_MFGACC,
		},
		.batt_info = {
			.voltage_max = 8800,	/* mV */
			.voltage_normal = 7700,
			.voltage_min = 6000,
			.precharge_current = 256,	/* mA */
			.start_charging_min_c = 0,
			.start_charging_max_c = 45,
			.charging_min_c = 0,
			.charging_max_c = 45,
			.discharging_min_c = -10,
			.discharging_max_c = 60,
		},
	},
	[BATTERY_SIMPLO_COS] = {
		.fuel_gauge = {
			.manuf_name = "333-1C-DA-A",
			.ship_mode = {
				.reg_addr = 0x00,
				.reg_data = { 0x0010, 0x0010 },
			},
			.fet = {
				.reg_addr = 0x0,
				.reg_mask = 0x6000,
				.disconnect_val = 0x6000,
			},
			.flags = FUEL_GAUGE_FLAG_MFGACC,
		},
		.batt_info = {
			.voltage_max = 8800,	/* mV */
			.voltage_normal = 7600,
			.voltage_min = 6000,
			.precharge_current = 256,	/* mA */
			.start_charging_min_c = 0,
			.start_charging_max_c = 45,
			.charging_min_c = 0,
			.charging_max_c = 45,
			.discharging_min_c = -10,
			.discharging_max_c = 60,
		},
	},
	[BATTERY_CosMX] = {
		.fuel_gauge = {
			.manuf_name = "333-AC-DA-A",
			.ship_mode = {
				.reg_addr = 0x00,
				.reg_data = { 0x0010, 0x0010 },
			},
			.fet = {
				.reg_addr = 0x0,
				.reg_mask = 0x6000,
				.disconnect_val = 0x6000,
			},
			.flags = FUEL_GAUGE_FLAG_MFGACC,
		},
		.batt_info = {
			.voltage_max = 8800,	/* mV */
			.voltage_normal = 7700,
			.voltage_min = 6000,
			.precharge_current = 256,	/* mA */
			.start_charging_min_c = 0,
			.start_charging_max_c = 45,
			.charging_min_c = 0,
			.charging_max_c = 45,
			.discharging_min_c = -10,
			.discharging_max_c = 60,
		},
	},
	[BATTERY_SAMSUNG_SDI] = {
		.fuel_gauge = {
			.manuf_name = "333-54-DA-A",
			.ship_mode = {
				.reg_addr = 0x00,
				.reg_data = { 0x0010, 0x0010 },
			},
			.fet = {
				.reg_addr = 0x0,
				.reg_mask = 0x6000,
				.disconnect_val = 0x6000,
			},
			.flags = FUEL_GAUGE_FLAG_MFGACC,
		},
		.batt_info = {
			.voltage_max = 8800,	/* mV */
			.voltage_normal = 7600,
			.voltage_min = 6000,
			.precharge_current = 256,	/* mA */
			.start_charging_min_c = 0,
			.start_charging_max_c = 45,
			.charging_min_c = 0,
			.charging_max_c = 45,
			.discharging_min_c = -10,
			.discharging_max_c = 60,
		},
	},
	[BATTERY_DYNAPACK_COS] = {
		.fuel_gauge = {
			.manuf_name = "333-2C-DA-A",
			.ship_mode = {
				.reg_addr = 0x00,
				.reg_data = { 0x0010, 0x0010 },
			},
			.fet = {
				.reg_addr = 0x0,
				.reg_mask = 0x6000,
				.disconnect_val = 0x6000,
			},
			.flags = FUEL_GAUGE_FLAG_MFGACC,
		},
		.batt_info = {
			.voltage_max = 8800,	/* mV */
			.voltage_normal = 7600,
			.voltage_min = 6000,
			.precharge_current = 256,	/* mA */
			.start_charging_min_c = 0,
			.start_charging_max_c = 45,
			.charging_min_c = 0,
			.charging_max_c = 45,
			.discharging_min_c = -10,
			.discharging_max_c = 60,
		},
	},
	[BATTERY_DYNAPACK_ATL] = {
		.fuel_gauge = {
			.manuf_name = "333-27-DA-A",
			.ship_mode = {
				.reg_addr = 0x00,
				.reg_data = { 0x0010, 0x0010 },
			},
			.fet = {
				.reg_addr = 0x0,
				.reg_mask = 0x6000,
				.disconnect_val = 0x6000,
			},
			.flags = FUEL_GAUGE_FLAG_MFGACC,
		},
		.batt_info = {
			.voltage_max = 8800,	/* mV */
			.voltage_normal = 7700,
			.voltage_min = 6000,
			.precharge_current = 256,	/* mA */
			.start_charging_min_c = 0,
			.start_charging_max_c = 45,
			.charging_min_c = 0,
			.charging_max_c = 45,
			.discharging_min_c = -10,
			.discharging_max_c = 60,
		},
	},
};
BUILD_ASSERT(ARRAY_SIZE(board_battery_info) == BATTERY_TYPE_COUNT);

const enum battery_type DEFAULT_BATTERY_TYPE = BATTERY_SIMPLO_HIGHPOWER;
