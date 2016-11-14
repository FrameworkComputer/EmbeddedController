/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Battery pack vendor provided charging profile
 */

#include "battery.h"
#include "battery_smart.h"
#include "bd9995x.h"
#include "charge_state.h"
#include "console.h"
#include "ec_commands.h"
#include "extpower.h"
#include "gpio.h"
#include "i2c.h"
#include "util.h"

#define ELECTRO_SHIP_MODE_REG 0x3a
#define ELECTRO_SHIP_MODE_DAT 0xC574

static enum battery_present batt_pres_prev = BP_NOT_SURE;

/* Battery info for BQ40Z55 */
static const struct battery_info info = {
	.voltage_max = 8700,	/* mV */
	.voltage_normal = 7600,

	/*
	 * Actual value 6000mV, added 100mV for charger accuracy so
	 * that unwanted low VSYS_Prochot# assertion can be avoided.
	 */
	.voltage_min = 6100,
	.precharge_current = 256,	/* mA */
	.start_charging_min_c = 0,
	.start_charging_max_c = 46,
	.charging_min_c = 0,
	.charging_max_c = 45,
	.discharging_min_c = 0,
	.discharging_max_c = 60,
};

static inline enum battery_present battery_hw_present(void)
{
	/* The GPIO is low when the battery is physically present */
	return gpio_get_level(GPIO_EC_BATT_PRES_L) ? BP_NO : BP_YES;
}

const struct battery_info *battery_get_info(void)
{
	return &info;
}

int board_cut_off_battery(void)
{
	int rv;

	/* Ship mode command must be sent twice to take effect */
	rv = sb_write(ELECTRO_SHIP_MODE_REG, ELECTRO_SHIP_MODE_DAT);
	if (rv != EC_SUCCESS)
		return rv;

	return sb_write(ELECTRO_SHIP_MODE_REG, ELECTRO_SHIP_MODE_DAT);
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
		rv = sb_write(SB_MANUFACTURER_ACCESS,
			      PARAM_OPERATION_STATUS);
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

		/*
		 * Battery is present and also the status is initialized and
		 * no safety fault, battery is disconnected.
		 */
		if (battery_is_present() == BP_YES)
			return BATTERY_DISCONNECTED;
	}
	not_disconnected = 1;
	return BATTERY_NOT_DISCONNECTED;
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

	charger_discharge_on_ac(!(curr->batt.flags & BATT_FLAG_WANT_CHARGE));

	/*
	 * Determine temperature range. The five ranges are:
	 *   < 0C
	 *    0C>= <=15C
	 *   15C>  <=20C
	 *   20C>  <=45C
	 *   > 45C
	 *
	 * If temp reading was bad, use last range.
	 */
	if (!(curr->batt.flags & BATT_FLAG_BAD_TEMPERATURE)) {
		if (temp_c < 0)
			temp_range = TEMP_RANGE_1;
		else if (temp_c <= 150)
			temp_range = TEMP_RANGE_2;
		else if (temp_c <= 200)
			temp_range = TEMP_RANGE_3;
		else if (temp_c <= 450)
			temp_range = TEMP_RANGE_4;
		else
			temp_range = TEMP_RANGE_5;
	}

	/*
	 * If battery voltage reading is bad, use the last reading.
	 */
	if (curr->batt.flags & BATT_FLAG_BAD_VOLTAGE) {
		batt_voltage = prev_batt_voltage;
	} else {
		batt_voltage = prev_batt_voltage = curr->batt.voltage;
		if (batt_voltage <= 8000)
			voltage_range = VOLTAGE_RANGE_LOW;
		else if (batt_voltage > 8000)
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
	 *
	 * When battery is < 0C:
	 * CC at 0mA @ 0V
	 * CV at 0V
	 *
	 * When battery is 0-15C:
	 * CC at 944mA until 8.0V @ 8.7V
	 * CC at 472mA @ 8.7V
	 * CV at 8.7V
	 *
	 * When battery is 15-20C:
	 * CC at 1416mA @ 8.7V
	 * CV at 8.7V
	 *
	 * When battery is 20-45C:
	 * CC at 3300mA @ 8.7V
	 * CV at 8.7V
	 *
	 * When battery is > 45C:
	 * CC at 0mA @ 0V
	 * CV at 0V
	 */
	switch (temp_range) {
	case TEMP_RANGE_2:
		if (voltage_range == VOLTAGE_RANGE_HIGH)
			curr->requested_current = 472;
		else
			curr->requested_current = 944;
		curr->requested_voltage = 8700;
		break;
	case TEMP_RANGE_3:
		curr->requested_current = 1416;
		curr->requested_voltage = 8700;
		break;
	case TEMP_RANGE_4:
		curr->requested_current = 3300;
		curr->requested_voltage = 8700;
		break;
	case TEMP_RANGE_1:
	case TEMP_RANGE_5:
	default:
		curr->requested_current = 0;
		curr->requested_voltage = 0;
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

#endif				/* CONFIG_CHARGER_PROFILE_OVERRIDE */

/*
 * Physical detection of battery.
 */
enum battery_present battery_is_present(void)
{
	enum battery_present batt_pres;
	int batt_status, rv;

	/* Get the physical hardware status */
	batt_pres = battery_hw_present();

	/*
	 * Make sure battery status is implemented, I2C transactions are
	 * success & the battery status is Initialized to find out if it
	 * is a working battery and it is not in the cut-off mode.
	 *
	 * If battery I2C fails but VBATT is high, battery is booting from
	 * cut-off mode.
	 *
	 * FETs are turned off after Power Shutdown time.
	 * The device will wake up when a voltage is applied to PACK.
	 * Battery status will be inactive until it is initialized.
	 */
	if (batt_pres == BP_YES && batt_pres_prev != batt_pres &&
		!battery_is_cut_off()) {
		rv = battery_status(&batt_status);
		if ((rv && bd9995x_get_battery_voltage() >= info.voltage_min) ||
			(!rv && !(batt_status & STATUS_INITIALIZED)))
			batt_pres = BP_NO;
	}

	batt_pres_prev = batt_pres;

	return batt_pres;
}

int board_battery_initialized(void)
{
	return battery_hw_present() == batt_pres_prev;
}
