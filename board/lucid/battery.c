/* Copyright 2015 The Chromium OS Authors. All rights reserved.
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
#include "i2c.h"
#include "util.h"

/* Shutdown mode parameter to write to manufacturer access register */
#define PARAM_CUT_OFF_LOW  0x10
#define PARAM_CUT_OFF_HIGH 0x00

/* Battery info for BQ40Z55 */
static const struct battery_info info = {
	.voltage_max = 4350,        /* mV */
	.voltage_normal = 3800,
	.voltage_min = 3000,
	.precharge_current = 256,   /* mA */
	.start_charging_min_c = 0,
	.start_charging_max_c = 55,
	.charging_min_c = 0,
	.charging_max_c = 60,
	.discharging_min_c = -20,
	.discharging_max_c = 60,
};

const struct battery_info *battery_get_info(void)
{
	return &info;
}

int board_cut_off_battery(void)
{
	int rv;
	uint8_t buf[3];

	/* Ship mode command must be sent twice to take effect */
	buf[0] = SB_MANUFACTURER_ACCESS & 0xff;
	buf[1] = PARAM_CUT_OFF_LOW;
	buf[2] = PARAM_CUT_OFF_HIGH;

	i2c_lock(I2C_PORT_BATTERY, 1);
	rv = i2c_xfer(I2C_PORT_BATTERY, BATTERY_ADDR, buf, 3, NULL, 0,
		      I2C_XFER_SINGLE);
	rv |= i2c_xfer(I2C_PORT_BATTERY, BATTERY_ADDR, buf, 3, NULL, 0,
		       I2C_XFER_SINGLE);
	i2c_lock(I2C_PORT_BATTERY, 0);

	return rv;
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
		TEMP_LOW,
		TEMP_NORMAL,
		TEMP_HIGH,
	} temp_range = TEMP_NORMAL;
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
	 *   < 15C
	 *   15-45C
	 *   > 45C
	 *
	 * Add 0.2 degrees of hysteresis.
	 * If temp reading was bad, use last range.
	 */
	if (!(curr->batt.flags & BATT_FLAG_BAD_TEMPERATURE)) {
		if (temp_c < 149)
			temp_range = TEMP_LOW;
		else if (temp_c > 151 && temp_c < 449)
			temp_range = TEMP_NORMAL;
		else if (temp_c > 451)
			temp_range = TEMP_HIGH;
	}

	/*
	 * If battery voltage reading is bad, use the last reading. Otherwise,
	 * determine voltage range with hysteresis.
	 */
	if (curr->batt.flags & BATT_FLAG_BAD_VOLTAGE) {
		batt_voltage = prev_batt_voltage;
	} else {
		batt_voltage = prev_batt_voltage = curr->batt.voltage;
		if (batt_voltage < 4050)
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
	 * When battery is 0-15C:
	 * CC at 1.8A @ 4.35V
	 * CV at 4.35V
	 *
	 * When battery is <45C:
	 * CC at 6A until 4.15V @ 4.35V
	 * CC at 3A @ 4.35V
	 * CV at 4.35V until current drops to 3A
	 *
	 * When battery is >45C:
	 * CC at 4.2A @ 4.1V
	 * CV at 4.1V (when battery is hot we don't go to fully charged)
	 */
	switch (temp_range) {
	case TEMP_LOW:
		curr->requested_current = 1800;
		curr->requested_voltage = 4350;
		break;
	case TEMP_NORMAL:
		curr->requested_voltage = 4350;
		if (voltage_range == VOLTAGE_RANGE_LOW)
			curr->requested_current = 6000;
		else
			curr->requested_current = 3000;
		break;
	case TEMP_HIGH:
		curr->requested_current = 4200;
		curr->requested_voltage = 4100;
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

#endif	/* CONFIG_CHARGER_PROFILE_OVERRIDE */
