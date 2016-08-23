/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Battery pack vendor provided charging profile
 */

#include "battery.h"
#include "charge_state.h"
#include "console.h"
#include "ec_commands.h"
#include "i2c.h"
#include "util.h"

/* Battery temperature ranges in degrees C */
static const struct battery_info info = {
	/* Design voltage */
	.voltage_max    = 4350,
	.voltage_normal = 3800,
	.voltage_min    = 2800,
	/* Pre-charge current: I <= 0.01C */
	.precharge_current  = 64,  /* mA */
	/* Operational temperature range */
	.start_charging_min_c = 5,
	.start_charging_max_c = 48,
	.charging_min_c       = 5,
	.charging_max_c       = 48,
	.discharging_min_c    = -20,
	.discharging_max_c    = 60,
};

const struct battery_info *battery_get_info(void)
{
	return &info;
}

int board_cut_off_battery(void)
{
	/* Write SET_SHUTDOWN(0x13) to CTRL(0x00) */
	return i2c_write16(I2C_PORT_BATTERY, 0xaa, 0x0, 0x13);
}

#ifdef CONFIG_CHARGER_PROFILE_OVERRIDE

static int fast_charging_allowed = 1;

/*
 * This can override the smart battery's charging profile. To make a change,
 * modify one or more of requested_voltage, requested_current, or state.
 * Leave everything else unchanged.
 *
 * Return the next poll period in usec, or zero to use the default (which is
 * state dependent).
 */
int charger_profile_override(struct charge_state_data *curr)
{
	/* temp in 0.1 deg C */
	int temp_c = curr->batt.temperature - 2731;
	/* keep track of last temperature range for hysteresis */
	static enum {
		TEMP_RANGE_1,
		TEMP_RANGE_2,
		TEMP_RANGE_3,
		TEMP_RANGE_4,
		TEMP_RANGE_5,
	} temp_range = TEMP_RANGE_3;
	/* keep track of last voltage range for hysteresis */
	static enum {
		VOLTAGE_RANGE_LOW,
		VOLTAGE_RANGE_HIGH,
	} voltage_range = VOLTAGE_RANGE_LOW;

	/* Current and previous battery voltage */
	int batt_voltage;
	static int prev_batt_voltage;

	/*
	 * Determine temperature range. The five ranges are:
	 *   < 10C
	 *   10-15C
	 *   15-23C
	 *   23-45C
	 *   > 45C
	 *
	 * Add 0.2 degrees of hysteresis.
	 * If temp reading was bad, use last range.
	 */
	if (!(curr->batt.flags & BATT_FLAG_BAD_TEMPERATURE)) {
		/* Don't charge if outside of allowable temperature range */
		if (temp_c >= info.charging_max_c * 10 ||
		    temp_c < info.charging_min_c * 10) {
			curr->requested_current = 0;
			curr->requested_voltage = 0;
			return 0;
		}


		if (temp_c < 99)
			temp_range = TEMP_RANGE_1;
		else if (temp_c > 101 && temp_c < 149)
			temp_range = TEMP_RANGE_2;
		else if (temp_c > 151 && temp_c < 229)
			temp_range = TEMP_RANGE_3;
		else if (temp_c > 231 && temp_c < 449)
			temp_range = TEMP_RANGE_4;
		else if (temp_c > 451)
			temp_range = TEMP_RANGE_5;
	}

	/*
	 * If battery voltage reading is bad, use the last reading. Otherwise,
	 * determine voltage range with 20mV * hysteresis.
	 */
	if (curr->batt.flags & BATT_FLAG_BAD_VOLTAGE) {
		batt_voltage = prev_batt_voltage;
	} else {
		batt_voltage = prev_batt_voltage = curr->batt.voltage;
		if (batt_voltage < 4130)
			voltage_range = VOLTAGE_RANGE_LOW;
		else if (batt_voltage > 4150)
			voltage_range = VOLTAGE_RANGE_HIGH;
	}

	/*
	 * If we are not charging or we aren't using fast charging profiles,
	 * then do not override desired current and voltage.
	 */
	if (curr->state != ST_CHARGE || !fast_charging_allowed)
		return 0;

	/*
	 * Okay, impose our custom will:
	 * When battery is 5-10C:
	 * CC at 900mA @ 4.35V
	 * CV at 4.35V until current drops to 450mA
	 *
	 * When battery is <15C:
	 * CC at 2700mA @ 4.35V
	 * CV at 4.35V until current drops to 450mA
	 *
	 * When battery is <23C:
	 * CC at 6300mA until 4.15V @ 4.35V
	 * CC at 4500mA @ 4.35V
	 * CV at 4.35V until current drops to 450mA
	 *
	 * When battery is <45C:
	 * CC at 9000mA until 4.15V @ 4.35V
	 * CC at 4500mA @ 4.35V
	 * CV at 4.35V until current drops to 450mA
	 *
	 * When battery is >45C:
	 * CC at 4500mA @ 4.15V
	 * CV at 4.15V (when battery is hot we don't go to fully charged)
	 */
	switch (temp_range) {
	case TEMP_RANGE_1:
		curr->requested_current = 900;
		curr->requested_voltage = 4350;
		break;
	case TEMP_RANGE_2:
		curr->requested_current = 2700;
		curr->requested_voltage = 4350;
		break;
	case TEMP_RANGE_3:
		curr->requested_voltage = 4350;
		if (voltage_range == VOLTAGE_RANGE_HIGH)
			curr->requested_current = 4500;
		else
			curr->requested_current = 6300;
		break;
	case TEMP_RANGE_4:
		curr->requested_voltage = 4350;
		if (voltage_range == VOLTAGE_RANGE_HIGH)
			curr->requested_current = 4500;
		else
			curr->requested_current = 9000;
		break;
	case TEMP_RANGE_5:
		curr->requested_current = 4500;
		curr->requested_voltage = 4150;
		break;
	}

	return 0;
}

/* Customs options controllable by host command. */
#define PARAM_FASTCHARGE (CS_PARAM_CUSTOM_PROFILE_MIN + 0)

enum ec_status charger_profile_override_get_param(uint32_t param,
						  uint32_t *value)
{
	if (param == PARAM_FASTCHARGE) {
		*value = fast_charging_allowed;
		return EC_RES_SUCCESS;
	}
	return EC_RES_INVALID_PARAM;
}

enum ec_status charger_profile_override_set_param(uint32_t param,
						  uint32_t value)
{
	if (param == PARAM_FASTCHARGE) {
		fast_charging_allowed = value;
		return EC_RES_SUCCESS;
	}
	return EC_RES_INVALID_PARAM;
}

#ifdef CONFIG_CMD_FASTCHARGE
static int command_fastcharge(int argc, char **argv)
{
	if (argc > 1 && !parse_bool(argv[1], &fast_charging_allowed))
		return EC_ERROR_PARAM1;

	ccprintf("fastcharge %s\n", fast_charging_allowed ? "on" : "off");

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(fastcharge, command_fastcharge,
			"[on|off]",
			"Get or set fast charging profile");
#endif  /* CONFIG_CMD_FASTCHARGE */

#endif	/* CONFIG_CHARGER_PROFILE_OVERRIDE */
