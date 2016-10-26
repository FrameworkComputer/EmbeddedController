/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Battery pack vendor provided charging profile
 */

#include "battery.h"
#include "battery_smart.h"
#include "charge_state.h"
#include "console.h"
#include "ec_commands.h"
#include "extpower.h"
#include "util.h"

/* Shutdown mode parameter to write to manufacturer access register */
#define SB_SHUTDOWN_DATA	0x0010

#ifdef BOARD_KEVIN
static const struct battery_info info = {
	.voltage_max		= 8688, /* 8700mA, round down for chg reg */
	.voltage_normal		= 7600,
	.voltage_min		= 6000,
	.precharge_current	= 200,
	.start_charging_min_c	= 0,
	.start_charging_max_c	= 45,
	.charging_min_c		= 0,
	.charging_max_c		= 60,
	.discharging_min_c	= -20,
	.discharging_max_c	= 70,
};
#elif defined(BOARD_GRU)
static const struct battery_info info = {
	.voltage_max		= 8688, /* 8700mA, round down for chg reg */
	.voltage_normal		= 7600,
	.voltage_min		= 5800,
	.precharge_current	= 256,
	.start_charging_min_c	= 0,
	.start_charging_max_c	= 50,
	.charging_min_c		= 0,
	.charging_max_c		= 50,
	.discharging_min_c	= -20,
	.discharging_max_c	= 60,
};
#endif

const struct battery_info *battery_get_info(void)
{
	return &info;
}

int board_cut_off_battery(void)
{
	int rv;

	/* Ship mode command must be sent twice to take effect */
	rv = sb_write(SB_MANUFACTURER_ACCESS, SB_SHUTDOWN_DATA);
	if (rv != EC_SUCCESS)
		return EC_RES_ERROR;

	rv = sb_write(SB_MANUFACTURER_ACCESS, SB_SHUTDOWN_DATA);
	return rv ? EC_RES_ERROR : EC_RES_SUCCESS;
}

enum battery_disconnect_state battery_get_disconnect_state(void)
{
	uint8_t data[6];
	int rv;

	/*
	 * Take note if we find that the battery isn't in disconnect state,
	 * and always return NOT_DISCONNECTED without probing the battery.
	 * This assumes the battery will not go to disconnect state during
	 * runtime.
	 */
	static int not_disconnected;

	if (not_disconnected)
		return BATTERY_NOT_DISCONNECTED;

	if (extpower_is_present()) {
		/* Check if battery charging + discharging is disabled. */
		rv = sb_write(SB_MANUFACTURER_ACCESS, PARAM_OPERATION_STATUS);
		if (rv)
			return BATTERY_DISCONNECT_ERROR;

		rv = sb_read_string(I2C_PORT_BATTERY, BATTERY_ADDR,
				    SB_ALT_MANUFACTURER_ACCESS, data, 6);

		if (rv || (~data[3] & (BATTERY_DISCHARGING_DISABLED |
				       BATTERY_CHARGING_DISABLED))) {
			not_disconnected = 1;
			return BATTERY_NOT_DISCONNECTED;
		}

		/*
		 * Battery is neither charging nor discharging. Verify that
		 * we didn't enter this state due to a safety fault.
		 */
		rv = sb_write(SB_MANUFACTURER_ACCESS, PARAM_SAFETY_STATUS);
		if (rv)
			return BATTERY_DISCONNECT_ERROR;

		rv = sb_read_string(I2C_PORT_BATTERY, BATTERY_ADDR,
				    SB_ALT_MANUFACTURER_ACCESS, data, 6);

		if (rv || data[2] || data[3] || data[4] || data[5])
			return BATTERY_DISCONNECT_ERROR;

		/* No safety fault, battery is disconnected */
		return BATTERY_DISCONNECTED;
	}
	not_disconnected = 1;
	return BATTERY_NOT_DISCONNECTED;
}

int charger_profile_override(struct charge_state_data *curr)
{
	static int prev_state = ST_IDLE;
	const struct battery_info *batt_info = battery_get_info();

	/* battery temp in 0.1 deg C */
	int bat_temp_c = curr->batt.temperature - 2731;

	if (curr->state == ST_CHARGE) {
		/* Don't charge if outside of allowable temperature range */
		if (bat_temp_c >= batt_info->charging_max_c * 10 ||
		    bat_temp_c < batt_info->charging_min_c * 10 ||
		    /* Don't start charging if battery is nearly full */
		    (prev_state != ST_CHARGE &&
		     curr->batt.state_of_charge > 95) ||
		    /* Don't charge if battery voltage is approaching max */
		    curr->batt.voltage > batt_info->voltage_max - 10) {
			curr->requested_current = curr->requested_voltage = 0;
			curr->batt.flags &= ~BATT_FLAG_WANT_CHARGE;
			curr->state = ST_IDLE;
		}
	}

	prev_state = curr->state;
	return 0;
}

/* Customs options controllable by host command. */
#define PARAM_FASTCHARGE (CS_PARAM_CUSTOM_PROFILE_MIN + 0)

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
