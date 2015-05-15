/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Battery pack vendor provided charging profile
 */

#include "battery.h"
#include "battery_smart.h"

/* Shutdown mode parameter to write to manufacturer access register */
#define SB_SHUTDOWN_DATA	0x0010

/* 3S1P battery */
static const struct battery_info info = {
	.voltage_max    = 12600,	/* mV */
	.voltage_normal = 10800,
	.voltage_min    = 8250,
	/*
	 * Precharge current max is 400 mA, but 384 mA is as close to that
	 * as the charger can get without going over.
	 */
	.precharge_current  = 384,	/* mA */
	.start_charging_min_c = 10,
	.start_charging_max_c = 45,
	.charging_min_c       = 10,
	.charging_max_c       = 45,
	.discharging_min_c    = -20,
	.discharging_max_c    = 60,
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
		return rv;

	return sb_write(SB_MANUFACTURER_ACCESS, SB_SHUTDOWN_DATA);
}
