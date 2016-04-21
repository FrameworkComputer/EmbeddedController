/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Battery pack vendor provided charging profile
 */

#include "battery.h"
#include "battery_smart.h"
#include "util.h"

/* Shutdown mode parameter to write to manufacturer access register */
#define SB_SHIP_MODE_REG	0x3a
#define SB_SHUTDOWN_DATA	0xC574

static const struct battery_info info = {
	.voltage_max = 13050,
	.voltage_normal = 11400,
	/*
	 * TODO(crosbug.com/p/44428):
	 * Support 2S battery for dev board.
	 * Should set voltage_min to 9V, when 2S battery phased out.
	 */
	.voltage_min = 6000,
	/* Pre-charge values. */
	.precharge_current = 256, /* mA */

	.start_charging_min_c = 0,
	.start_charging_max_c = 45,
	.charging_min_c = 0,
	.charging_max_c = 45,
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

	/* Ship mode command must be sent twice to take effect */
	rv = sb_write(SB_SHIP_MODE_REG, SB_SHUTDOWN_DATA);

	if (rv != EC_SUCCESS)
		return rv;

	return sb_write(SB_SHIP_MODE_REG, SB_SHUTDOWN_DATA);
}
