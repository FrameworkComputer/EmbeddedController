/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Battery pack vendor provided charging profile
 */

#include "battery.h"
#include "battery_smart.h"

/* Shutdown mode parameter to write to manufacturer access register */
#define SB_SHIP_MODE_REG	SB_MANUFACTURER_ACCESS
#define SB_SHUTDOWN_DATA	0x0010

/* Battery info */
static const struct battery_info info = {
	.voltage_max = 8800,
	.voltage_normal = 7700,
	.voltage_min = 6000,
	.precharge_current = 256, /* mA */
	.start_charging_min_c = 0,
	.start_charging_max_c = 50,
	.charging_min_c = 0,
	.charging_max_c = 60,
	.discharging_min_c = -20,
	.discharging_max_c = 75,
};

const struct battery_info *battery_get_info(void)
{
	return &info;
}

int board_cut_off_battery(void)
{
	int rv;

	/* Ship mode command must be sent twice to take effect */
	rv = sb_write(SB_SHIP_MODE_REG, SB_SHUTDOWN_DATA);

	if (rv != EC_SUCCESS)
		return rv;

	return sb_write(SB_SHIP_MODE_REG, SB_SHUTDOWN_DATA);
}
