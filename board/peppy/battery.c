/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Battery pack vendor provided charging profile
 */

#include "battery.h"
#include "battery_smart.h"
#include "gpio.h"
#include "host_command.h"

#define SB_SHIP_MODE_ADDR	0x3a
#define SB_SHIP_MODE_DATA	0xc574

/* Values for 54Wh 3UPF656790-1-T1001 battery */
static const struct battery_info info = {

	.voltage_max    = 12600,
	.voltage_normal = 11100, /* Average of max & min */
	.voltage_min    =  9000,

	/* Pre-charge values. */
	.precharge_current  = 392,	/* mA */

	.start_charging_min_c = 0,
	.start_charging_max_c = 60,
	.charging_min_c       = 0,
	.charging_max_c       = 60,
	.discharging_min_c    = 0,
	.discharging_max_c    = 50,
};

const struct battery_info *battery_get_info(void)
{
	return &info;
}

int battery_command_cut_off(struct host_cmd_handler_args *args)
{
	return sb_write(SB_SHIP_MODE_ADDR, SB_SHIP_MODE_DATA);
}
DECLARE_HOST_COMMAND(EC_CMD_BATTERY_CUT_OFF, battery_command_cut_off,
		     EC_VER_MASK(0));
