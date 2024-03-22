/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "battery.h"
#include "battery_fuel_gauge.h"
#include "battery_smart.h"
#include "charge_state.h"
#include "console.h"
#include "gpio.h"
#include "temp_sensor.h"
#include "util.h"

const struct batt_conf_embed board_battery_info[] = {
	/* DynaPack CosMX Battery Information */
	[BATTERY_DYNAPACK_COS] = {
		.manuf_name = "333-2C-DA-A",
		.config = {
			.fuel_gauge = {
				.ship_mode = {
					.reg_addr = 0x00,
					.reg_data = { 0x0010, 0x0010 },
				},
				.fet = {
					.reg_addr = 0x0,
					.reg_mask = 0x0006,
					.disconnect_val = 0x0,
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
				.vendor_param_start = 0x70,
			},
		},
	},

	/* DynaPack ATL Battery Information */
	[BATTERY_DYNAPACK_ATL] = {
		.manuf_name = "333-27-DA-A",
		.config = {
			.fuel_gauge = {
				.ship_mode = {
					.reg_addr = 0x00,
					.reg_data = { 0x0010, 0x0010 },
				},
				.fet = {
					.reg_addr = 0x0,
					.reg_mask = 0x0006,
					.disconnect_val = 0x0,
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
				.vendor_param_start = 0x70,
			},
		},
	},

	/* Simplo CosMX Battery Information */
	[BATTERY_SIMPLO_COS] = {
		.manuf_name = "333-1C-DA-A",
		.config = {
			.fuel_gauge = {
				.ship_mode = {
					.reg_addr = 0x00,
					.reg_data = { 0x0010, 0x0010 },
				},
				.fet = {
					.reg_addr = 0x0,
					.reg_mask = 0x0006,
					.disconnect_val = 0x0,
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
				.vendor_param_start = 0x70,
			},
		},
	},

	/* Simplo HIGHPOWER Battery Information */
	[BATTERY_SIMPLO_HIGHPOWER] = {
		.manuf_name = "333-1D-DA-A",
		.config = {
			.fuel_gauge = {
				.ship_mode = {
					.reg_addr = 0x00,
					.reg_data = { 0x0010, 0x0010 },
				},
				.fet = {
					.reg_addr = 0x0,
					.reg_mask = 0x0006,
					.disconnect_val = 0x0,
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
				.vendor_param_start = 0x70,
			},
		},
	},

	/* CosMX B00C4473A9D0002 Battery Information */
	[BATTERY_COS] = {
		.manuf_name = "333-AC-DA-A",
		.config = {
			.fuel_gauge = {
				.ship_mode = {
					.reg_addr = 0x00,
					.reg_data = { 0x0010, 0x0010 },
				},
				.fet = {
					.reg_addr = 0x0,
					.reg_mask = 0x0006,
					.disconnect_val = 0x0,
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
				.vendor_param_start = 0x70,
			},
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
		if (chg_temp <= temp_chg_table[chg_lvl].lo_thre && chg_lvl > 0)
			chg_lvl--;
		else if (chg_temp >= temp_chg_table[chg_lvl].hi_thre &&
			 chg_lvl < CHG_LEVEL_COUNT - 1)
			chg_lvl++;

		curr->requested_current = MIN(curr->requested_current,
					      temp_chg_table[chg_lvl].chg_curr);

		if (chg_lvl != prev_chg_lvl)
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

enum battery_present batt_pres_prev = BP_NOT_SURE;

/*
 * Physical detection of battery.
 */
static enum battery_present battery_check_present_status(void)
{
	enum battery_present batt_pres = BP_NOT_SURE;

	/* Get the physical hardware status */
	batt_pres = battery_hw_present();

	/*
	 * If the battery is not physically connected, then no need to perform
	 * any more checks.
	 */
	if (batt_pres == BP_NO)
		return batt_pres;

	/*
	 * If the battery is present now and was present last time we checked,
	 * return early.
	 */
	if (batt_pres == batt_pres_prev)
		return batt_pres;

	/*
	 * Check battery disconnect status. If we are unable to read battery
	 * disconnect status, then return BP_NOT_SURE. Battery could be in ship
	 * mode and might require pre-charge current to wake it up. BP_NO is not
	 * returned here because charger state machine will not provide
	 * pre-charge current assuming that battery is not present.
	 */
	if (battery_get_disconnect_state() == BATTERY_DISCONNECT_ERROR)
		return BP_NOT_SURE;

	/* Ensure the battery is not in cutoff state */
	if (battery_is_cut_off() != BATTERY_CUTOFF_STATE_NORMAL)
		return BP_NO;

	return batt_pres;
}

enum battery_present battery_is_present(void)
{
	batt_pres_prev = battery_check_present_status();
	return batt_pres_prev;
}

enum battery_present battery_hw_present(void)
{
	return gpio_get_level(GPIO_EC_BATT_PRES_ODL) ? BP_NO : BP_YES;
}
