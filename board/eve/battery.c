/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Placeholder values for temporary battery pack.
 */

#include "battery.h"
#include "battery_smart.h"
#include "bd9995x.h"
#include "charge_state.h"
#include "console.h"
#include "ec_commands.h"
#include "extpower.h"
#include "gpio.h"
#include "util.h"

/* Shutdown mode parameter to write to manufacturer access register */
#define SB_SHUTDOWN_DATA	0x0010

static enum battery_present batt_pres_prev = BP_NOT_SURE;

/* Battery info for proto */
static const struct battery_info info = {
	.voltage_max		= 8800,	/* mV */
	.voltage_normal		= 7700,
	.voltage_min		= 6100, /* Add 100mV for charger accuracy */
	.precharge_current	= 256,	/* mA */
	.start_charging_min_c	= 0,
	.start_charging_max_c	= 46,
	.charging_min_c		= 0,
	.charging_max_c		= 60,
	.discharging_min_c	= 0,
	.discharging_max_c	= 60,
};

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
		rv = sb_read_mfgacc(PARAM_OPERATION_STATUS,
				SB_ALT_MANUFACTURER_ACCESS, data, sizeof(data));
		if (rv)
			return BATTERY_DISCONNECT_ERROR;

		if (~data[3] & (BATTERY_DISCHARGING_DISABLED |
				BATTERY_CHARGING_DISABLED)) {
			not_disconnected = 1;
			return BATTERY_NOT_DISCONNECTED;
		}

		/*
		 * Battery is neither charging nor discharging. Verify that
		 * we didn't enter this state due to a safety fault.
		 */
		rv = sb_read_mfgacc(PARAM_SAFETY_STATUS,
				SB_ALT_MANUFACTURER_ACCESS, data, sizeof(data));
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

int charger_profile_override(struct charge_state_data *curr)
{
	const struct battery_info *batt_info;
	/* battery temp in 0.1 deg C */
	int bat_temp_c = curr->batt.temperature - 2731;

	batt_info = battery_get_info();
	/* Don't charge if outside of allowable temperature range */
	if (bat_temp_c >= batt_info->charging_max_c * 10 ||
	    bat_temp_c < batt_info->charging_min_c * 10) {
		curr->requested_current = 0;
		curr->requested_voltage = 0;
		curr->batt.flags &= ~BATT_FLAG_WANT_CHARGE;
		curr->state = ST_IDLE;
	}
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

static inline enum battery_present battery_hw_present(void)
{
	/* The GPIO is low when the battery is physically present */
	return gpio_get_level(GPIO_BATTERY_PRESENT_L) ? BP_NO : BP_YES;
}

static int battery_init(void)
{
	int batt_status;

	return battery_status(&batt_status) ? 0 :
		!!(batt_status & STATUS_INITIALIZED);
}

/*
 * Physical detection of battery.
 */
enum battery_present battery_is_present(void)
{
	enum battery_present batt_pres;

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
	    !battery_is_cut_off() && !battery_init()) {
		batt_pres = BP_NO;
	}

	batt_pres_prev = batt_pres;

	return batt_pres;
}

int board_battery_initialized(void)
{
	return battery_hw_present() == batt_pres_prev;
}
