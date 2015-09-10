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
	.voltage_max = 8700,        /* mV */
	.voltage_normal = 7600,
	.voltage_min = 6000,
	.precharge_current = 256,   /* mA */
	.start_charging_min_c = 0,
	.start_charging_max_c = 46,
	.charging_min_c = 0,
	.charging_max_c = 60,
	.discharging_min_c = 0,
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
	 * determine voltage range with hysteresis.
	 */
	if (curr->batt.flags & BATT_FLAG_BAD_VOLTAGE) {
		batt_voltage = prev_batt_voltage;
	} else {
		batt_voltage = prev_batt_voltage = curr->batt.voltage;
		if (batt_voltage < 8200)
			voltage_range = VOLTAGE_RANGE_LOW;
		else if (batt_voltage > 8300)
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
	 * When battery is 0-10C:
	 * CC at 486mA @ 8.7V
	 * CV at 8.7V
	 *
	 * When battery is <15C:
	 * CC at 1458mA @ 8.7V
	 * CV at 8.7V
	 *
	 * When battery is <23C:
	 * CC at 3402mA until 8.3V @ 8.7V
	 * CC at 2430mA @ 8.7V
	 * CV at 8.7V
	 *
	 * When battery is <45C:
	 * CC at 4860mA until 8.3V @ 8.7V
	 * CC at 2430mA @ 8.7V
	 * CV at 8.7V until current drops to 450mA
	 *
	 * When battery is >45C:
	 * CC at 2430mA @ 8.3V
	 * CV at 8.3V (when battery is hot we don't go to fully charged)
	 */
	switch (temp_range) {
	case TEMP_RANGE_1:
		curr->requested_current = 486;
		curr->requested_voltage = 8700;
		break;
	case TEMP_RANGE_2:
		curr->requested_current = 1458;
		curr->requested_voltage = 8700;
		break;
	case TEMP_RANGE_3:
		curr->requested_voltage = 8700;
		if (voltage_range == VOLTAGE_RANGE_HIGH)
			curr->requested_current = 2430;
		else
			curr->requested_current = 3402;
		break;
	case TEMP_RANGE_4:
		curr->requested_voltage = 8700;
		if (voltage_range == VOLTAGE_RANGE_HIGH)
			curr->requested_current = 2430;
		else
			curr->requested_current = 4860;
		break;
	case TEMP_RANGE_5:
		curr->requested_current = 2430;
		curr->requested_voltage = 8300;
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
			"Get or set fast charging profile",
			NULL);

#endif	/* CONFIG_CHARGER_PROFILE_OVERRIDE */
