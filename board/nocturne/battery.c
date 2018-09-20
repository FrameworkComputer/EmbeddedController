/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Battery pack vendor provided charging profile
 */

#include "battery.h"
#include "battery_smart.h"
#include "charge_manager.h"
#include "chipset.h"
#include "common.h"
#include "console.h"
#include "ec_commands.h"
#include "extpower.h"
#include "hooks.h"
#include "usb_pd.h"

/* Shutdown mode parameter to write to manufacturer access register */
#define SB_SHUTDOWN_DATA	0x0010

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
