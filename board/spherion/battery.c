/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "battery.h"
#include "battery_fuel_gauge.h"
#include "battery_smart.h"
#include "charge_state.h"
#include "chipset.h"
#include "gpio.h"
#include "temp_sensor.h"
#include "util.h"

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
	/* Panasonic AP1505L Battery Information */
	[BATTERY_PANASONIC_AP15O5L] = {
		.manuf_name = "PANASONIC KT00305013",
		.device_name = "AP15O5L",
		.config = {
			.fuel_gauge = {
				.ship_mode = {
					.reg_addr = 0x3A,
					.reg_data = { 0xC574, 0xC574 },
				},
				.fet = {
					.reg_addr = 0x0,
					.reg_mask = 0x4000,
					.disconnect_val = 0x0,
				},
			},
			.batt_info = {
				.voltage_max		= 13200,
				.voltage_normal		= 11550, /* mV */
				.voltage_min		= 9000, /* mV */
				.precharge_current	= 256,	/* mA */
				.start_charging_min_c	= 0,
				.start_charging_max_c	= 50,
				.charging_min_c		= 0,
				.charging_max_c		= 60,
				.discharging_min_c	= -20,
				.discharging_max_c	= 75,
			},
		},
	},
};
BUILD_ASSERT(ARRAY_SIZE(board_battery_info) == BATTERY_TYPE_COUNT);

const enum battery_type DEFAULT_BATTERY_TYPE = BATTERY_PANASONIC_AP15O5L;

int charger_profile_override(struct charge_state_data *curr)
{
	int charger_temp, charger_temp_c;
	int on;

	/* charge confrol if the system is on, otherwise turn it off */
	on = chipset_in_state(CHIPSET_STATE_ON);
	if (!on)
		return 0;

	/* charge control if outside of allowable temperature range */
	if (curr->state == ST_CHARGE) {
		temp_sensor_read(TEMP_SENSOR_CHARGER, &charger_temp);
		charger_temp_c = K_TO_C(charger_temp);
		if (charger_temp_c > 52)
			curr->requested_current =
				MIN(curr->requested_current, 2200);
		else if (charger_temp_c > 48)
			curr->requested_current =
				MIN(curr->requested_current,
				    CONFIG_CHARGER_MAX_INPUT_CURRENT);
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
