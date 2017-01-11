/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Battery pack vendor provided charging profile
 */

#include "battery.h"
#include "battery_smart.h"
#include "bd9995x.h"
#include "charge_ramp.h"
#include "charge_state.h"
#include "console.h"
#include "ec_commands.h"
#include "extpower.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "system.h"
#include "i2c.h"
#include "util.h"

/* FET ON/OFF cammand write to fet off register */
#define SB_FET_OFF      0x34
#define SB_FETOFF_DATA1 0x0000
#define SB_FETOFF_DATA2 0x1000
#define SB_FETON_DATA1  0x2000
#define SB_FETON_DATA2  0x4000
#define BATTERY_FETOFF  0x0100

/* First use day base */
#define BATT_FUD_BASE   0x38

#define GREEN_BOOK_SUPPORT      (1 << 2)

/* Shutdown mode parameter to write to manufacturer access register */
#define PARAM_CUT_OFF_LOW  0x10
#define PARAM_CUT_OFF_HIGH 0x00

static enum battery_present batt_pres_prev = BP_NOT_SURE;

/* Battery info for BQ40Z55 */
static const struct battery_info info = {
	/* FIXME(dhendrix): where do these values come from? */
	.voltage_max = 13050,	/* mV */
	.voltage_normal = 11250,
	.voltage_min = 9000,
	.precharge_current = 200,	/* mA */
	.start_charging_min_c = 0,
	.start_charging_max_c = 45,
	.charging_min_c = 0,
	.charging_max_c = 60,
	.discharging_min_c = 0,
	.discharging_max_c = 70,
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

static void wakeup(void)
{
	int d;
	int mode;

	/* Add Green Book support */
	if (sb_read(SB_BATTERY_MODE, &mode) == EC_RES_SUCCESS) {
		mode |= GREEN_BOOK_SUPPORT;
		sb_write(SB_BATTERY_MODE, mode);
	}

	if (sb_read(SB_FET_OFF, &d) == EC_RES_SUCCESS) {
		if (extpower_is_present() && (d == BATTERY_FETOFF)) {
			sb_write(SB_FET_OFF, SB_FETON_DATA1);
			sb_write(SB_FET_OFF, SB_FETON_DATA2);
		}
	}
}
DECLARE_HOOK(HOOK_INIT, wakeup, HOOK_PRIO_DEFAULT);

static int cutoff(void)
{
	int rv;

	/* Ship mode command must be sent twice to take effect */
	rv = sb_write(SB_FET_OFF, SB_FETOFF_DATA1);

	if (rv != EC_SUCCESS)
		return rv;

	return sb_write(SB_FET_OFF, SB_FETOFF_DATA2);
}

int board_cut_off_battery(void)
{
	return cutoff();
}

int battery_get_vendor_param(uint32_t param, uint32_t *value)
{
	return EC_ERROR_UNIMPLEMENTED;
}

/* parameter 0 for first use day */
int battery_set_vendor_param(uint32_t param, uint32_t value)
{
	if (param == 0) {
		int rv, ymd;

		rv = sb_read(BATT_FUD_BASE, &ymd);
		if (rv != EC_SUCCESS)
			return EC_ERROR_UNKNOWN;
		if (ymd == 0)
			return sb_write(BATT_FUD_BASE, value) ?
					EC_ERROR_UNKNOWN : EC_SUCCESS;

		rv = sb_read(BATT_FUD_BASE | 0x03, &ymd);
		if (rv != EC_SUCCESS)
			return EC_ERROR_UNKNOWN;
		if (ymd == 0)
			return sb_write(BATT_FUD_BASE | 0x03, value) ?
					EC_ERROR_UNKNOWN : EC_SUCCESS;

		rv = sb_read(BATT_FUD_BASE | 0x07, &ymd);
		if (rv != EC_SUCCESS)
			return EC_ERROR_UNKNOWN;
		if (ymd == 0)
			return sb_write(BATT_FUD_BASE | 0x07, value) ?
					EC_ERROR_UNKNOWN : EC_SUCCESS;

		return EC_ERROR_UNKNOWN;
	} else {
		return EC_ERROR_UNIMPLEMENTED;
	}
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

#ifdef CONFIG_CHARGER_PROFILE_OVERRIDE

static int charger_should_discharge_on_ac(struct charge_state_data *curr)
{
	/* can not discharge on AC without battery */
	if (curr->batt.is_present != BP_YES)
		return 0;

	/* Do not discharge on AC if the battery is still waking up */
	if (!(curr->batt.flags & BATT_FLAG_WANT_CHARGE) &&
		!(curr->batt.status & STATUS_FULLY_CHARGED))
		return 0;

	/*
	 * In light load (<450mA being withdrawn from VSYS) the DCDC of the
	 * charger operates intermittently i.e. DCDC switches continuously
	 * and then stops to regulate the output voltage and current, and
	 * sometimes to prevent reverse current from flowing to the input.
	 * This causes a slight voltage ripple on VSYS that falls in the
	 * audible noise frequency (single digit kHz range). This small
	 * ripple generates audible noise in the output ceramic capacitors
	 * (caps on VSYS and any input of DCDC under VSYS).
	 *
	 * To overcome this issue enable the battery learning operation
	 * and suspend USB charging and DC/DC converter.
	 */
	if (!battery_is_cut_off() &&
		!(curr->batt.flags & BATT_FLAG_WANT_CHARGE) &&
		(curr->batt.status & STATUS_FULLY_CHARGED))
		return 1;

	/*
	 * To avoid inrush current from the external charger, enable
	 * discharge on AC till the new charger is detected and charge
	 * detect delay has passed.
	 */
	if (!chg_ramp_is_detected() && curr->batt.state_of_charge > 2)
		return 1;

	return 0;
}

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
	int disch_on_ac = charger_should_discharge_on_ac(curr);

	charger_discharge_on_ac(disch_on_ac);

	if (disch_on_ac) {
		curr->state = ST_DISCHARGE;
		return 0;
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
