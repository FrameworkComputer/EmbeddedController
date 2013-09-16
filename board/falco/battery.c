/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Battery pack vendor provided charging profile
 */

#include "battery_pack.h"
#include "host_command.h"
#include "smart_battery.h"

#define SB_SHIP_MODE_DATA	0x0010

/* FIXME: We need REAL values for all this stuff */
const struct battery_temperature_ranges bat_temp_ranges = {
	.start_charging_min_c = 0,
	.start_charging_max_c = 45,
	.charging_min_c       = 0,
	.charging_max_c       = 45,
	.discharging_min_c    = -10,
	.discharging_max_c    = 60,
};

static const struct battery_info info = {

	.voltage_max    = 8400,
	.voltage_normal = 7400,
	.voltage_min    = 6000,

	/* Pre-charge values. */
	.precharge_current  = 256,	/* mA */
};

const struct battery_info *battery_get_info(void)
{
	return &info;
}

int battery_command_cut_off(struct host_cmd_handler_args *args)
{
	int rv;

	/* Ship mode command must be sent twice. */
	rv = sb_write(SB_MANUFACTURER_ACCESS, SB_SHIP_MODE_DATA);
	if (rv != EC_SUCCESS)
		return rv;
	rv = sb_write(SB_MANUFACTURER_ACCESS, SB_SHIP_MODE_DATA);
	return rv;
}
DECLARE_HOST_COMMAND(EC_CMD_BATTERY_CUT_OFF, battery_command_cut_off,
		     EC_VER_MASK(0));
