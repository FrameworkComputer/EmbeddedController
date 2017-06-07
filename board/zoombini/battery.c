/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Battery pack vendor provided charging profile
 */

#include "battery.h"

/* Battery info for proto */
static const struct battery_info info = {
	.voltage_max		= 13200,
	.voltage_normal		= 11250,
	.voltage_min		= 9000,
	.precharge_current	= 189,
	.start_charging_min_c	= 0,
	.start_charging_max_c	= 60,
	.charging_min_c		= 0,
	.charging_max_c		= 60,
	.discharging_min_c	= -20,
	.discharging_max_c	= 60,
};

const struct battery_info *battery_get_info(void)
{
	return &info;
}
