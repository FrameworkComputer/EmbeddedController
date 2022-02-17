/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "battery.h"
#include "battery_fuel_gauge.h"
#include "battery_smart.h"
#include "charge_state.h"
#include "console.h"
#include "temp_sensor.h"
#include "util.h"

const struct board_batt_params board_battery_info[] = {
	/* DynaPack CosMX Battery Information */
	[BATTERY_DYNAPACK_COS] = {
		.fuel_gauge = {
			.manuf_name = "333-2C-DA-A",
			.ship_mode = {
				.reg_addr = 0x00,
				.reg_data = { 0x0010, 0x0010 },
			},
			.fet = {
				.mfgacc_support = 1,
				.reg_addr = 0x0,
				.reg_mask = 0x0006,
				.disconnect_val = 0x0,
			},
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
			.vendor_param_start = 0x70,
		},
	},

	/* DynaPack ATL Battery Information */
	[BATTERY_DYNAPACK_ATL] = {
		.fuel_gauge = {
			.manuf_name = "333-27-DA-A",
			.ship_mode = {
				.reg_addr = 0x00,
				.reg_data = { 0x0010, 0x0010 },
			},
			.fet = {
				.mfgacc_support = 1,
				.reg_addr = 0x0,
				.reg_mask = 0x0006,
				.disconnect_val = 0x0,
			},
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
			.vendor_param_start = 0x70,
		},
	},

	/* Simplo CosMX Battery Information */
	[BATTERY_SIMPLO_COS] = {
		.fuel_gauge = {
			.manuf_name = "333-1C-DA-A",
			.ship_mode = {
				.reg_addr = 0x00,
				.reg_data = { 0x0010, 0x0010 },
			},
			.fet = {
				.mfgacc_support = 1,
				.reg_addr = 0x0,
				.reg_mask = 0x0006,
				.disconnect_val = 0x0,
			},
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
			.vendor_param_start = 0x70,
		},
	},

	/* Simplo HIGHPOWER Battery Information */
	[BATTERY_SIMPLO_HIGHPOWER] = {
		.fuel_gauge = {
			.manuf_name = "333-1D-DA-A",
			.ship_mode = {
				.reg_addr = 0x00,
				.reg_data = { 0x0010, 0x0010 },
			},
			.fet = {
				.mfgacc_support = 1,
				.reg_addr = 0x0,
				.reg_mask = 0x0006,
				.disconnect_val = 0x0,
			},
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
			.vendor_param_start = 0x70,
		},
	},

	/* CosMX B00C4473A9D0002 Battery Information */
	[BATTERY_COS] = {
		.fuel_gauge = {
			.manuf_name = "333-AC-DA-A",
			.ship_mode = {
				.reg_addr = 0x00,
				.reg_data = { 0x0010, 0x0010 },
			},
			.fet = {
				.mfgacc_support = 1,
				.reg_addr = 0x0,
				.reg_mask = 0x0006,
				.disconnect_val = 0x0,
			},
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
			.vendor_param_start = 0x70,
		},
	},
};
BUILD_ASSERT(ARRAY_SIZE(board_battery_info) == BATTERY_TYPE_COUNT);

const enum battery_type DEFAULT_BATTERY_TYPE = BATTERY_DYNAPACK_COS;

int charger_profile_override(struct charge_state_data *curr)
{
	int chg_temp;
	int prev_chg_lvl;
	static int chg_lvl;

	if (chipset_in_state(CHIPSET_STATE_ON)) {
		temp_sensor_read(TEMP_SENSOR_CHARGER, &chg_temp);
		chg_temp = K_TO_C(chg_temp);

		prev_chg_lvl = chg_lvl;
		if (chg_temp <= temp_chg_table[chg_lvl].lo_thre &&
		    chg_lvl > 0)
			chg_lvl--;
		else if (chg_temp >= temp_chg_table[chg_lvl].hi_thre &&
		         chg_lvl < CHG_LEVEL_COUNT - 1)
			chg_lvl++;

		curr->requested_current = MIN(curr->requested_current,
		      temp_chg_table[chg_lvl].chg_curr);

		if(chg_lvl != prev_chg_lvl)
			ccprints("Override chg curr to %dmA by chg LEVEL_%d",
			         curr->requested_current, chg_lvl);
	}

	return 0;
}

enum ec_status charger_profile_override_get_param(uint32_t param,
						  uint32_t *value)
{
	return EC_RES_INVALID_PARAM;
}

enum ec_status charger_profile_override_set_param(uint32_t param,
						  uint32_t value)
{
	return EC_RES_INVALID_PARAM;
}
