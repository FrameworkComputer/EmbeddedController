/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Battery pack vendor provided charging profile
 */

#include "battery.h"
#include "battery_smart.h"
#include "common.h"
#include "ec_commands.h"
#include "extpower.h"

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
