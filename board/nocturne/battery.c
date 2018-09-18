/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Battery pack vendor provided charging profile
 */

#include "battery.h"
#include "battery_smart.h"
#include "charge_manager.h"
#include "charger.h"
#include "chipset.h"
#include "charge_state_v2.h"
#include "common.h"
#include "console.h"
#include "ec_commands.h"
#include "extpower.h"
#include "hooks.h"
#include "temp_sensor.h"
#include "usb_pd.h"

/* Shutdown mode parameter to write to manufacturer access register */
#define SB_SHUTDOWN_DATA	0x0010

/*
 * We need to stop charging the battery when the DRAM temperature sensor gets
 * over 47 C (320 K), and resume charging once it cools back down.
 */
#define DRAM_STOPCHARGE_TEMP_K	320

/* Battery info */
static const struct battery_info info = {
	.voltage_max		= 8880,
	.voltage_normal		= 7700,
	.voltage_min		= 6000,
	.precharge_current	= 160,
	.start_charging_min_c	= 10,
	.start_charging_max_c	= 50,
	.charging_min_c		= 10,
	.charging_max_c		= 50,
	.discharging_min_c	= -20,
	.discharging_max_c	= 60,
};

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

const struct battery_info *battery_get_info(void)
{
	return &info;
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

	/* Check if battery discharge FET is disabled. */
	rv = sb_read_mfgacc(PARAM_OPERATION_STATUS,
			    SB_ALT_MANUFACTURER_ACCESS, data, sizeof(data));
	if (rv)
		return BATTERY_DISCONNECT_ERROR;
	if (~data[3] & (BATTERY_DISCHARGING_DISABLED)) {
		not_disconnected = 1;
		return BATTERY_NOT_DISCONNECTED;
	}

	/*
	 * Battery discharge FET is disabled.  Verify that we didn't enter this
	 * state due to a safety fault.
	 */
	rv = sb_read_mfgacc(PARAM_SAFETY_STATUS,
			    SB_ALT_MANUFACTURER_ACCESS, data, sizeof(data));
	if (rv || data[2] || data[3] || data[4] || data[5])
		return BATTERY_DISCONNECT_ERROR;

	/* No safety fault, battery is disconnected */
	return BATTERY_DISCONNECTED;
}

static void reduce_input_voltage_when_full(void)
{
	struct batt_params batt;
	int max_pd_voltage_mv;
	int active_chg_port;

	active_chg_port = charge_manager_get_active_charge_port();
	if (active_chg_port == CHARGE_PORT_NONE)
		return;

	battery_get_params(&batt);
	if (!(batt.flags & BATT_FLAG_BAD_STATUS)) {
		/* Lower our input voltage to 9V when battery is full. */
		if ((batt.status & STATUS_FULLY_CHARGED) &&
		    chipset_in_state(CHIPSET_STATE_ANY_OFF))
			max_pd_voltage_mv = 9000;
		else
			max_pd_voltage_mv = PD_MAX_VOLTAGE_MV;

		if (pd_get_max_voltage() != max_pd_voltage_mv)
			pd_set_external_voltage_limit(active_chg_port,
						      max_pd_voltage_mv);
	}
}
DECLARE_HOOK(HOOK_SECOND, reduce_input_voltage_when_full, HOOK_PRIO_DEFAULT);

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

static int should_stopcharge(void)
{
	int t_dram;

	/* We can only stop charging on AC, if AC is plugged in. */
	if (!gpio_get_level(GPIO_AC_PRESENT))
		return 0;

	/*
	 * The DRAM temperature sensor is only available when the AP is on,
	 * therefore only inhibit charging when we can actually read a
	 * temperature.
	 */
	if (chipset_in_state(CHIPSET_STATE_ON) &&
	    !temp_sensor_read(TEMP_SENSOR_DRAM, &t_dram) &&
	    (t_dram >= DRAM_STOPCHARGE_TEMP_K))
		return 1;
	else
		return 0;
}

int charger_profile_override(struct charge_state_data *curr)
{
	static uint8_t stopcharge_on_ac;
	int enable_stopcharge;

	enable_stopcharge = should_stopcharge();
	if (enable_stopcharge != stopcharge_on_ac) {
		stopcharge_on_ac = enable_stopcharge;
		if (enable_stopcharge) {
			chgstate_set_manual_current(0);
		} else {
			chgstate_set_manual_current(-1);
		}
	}

	return 0;
}
