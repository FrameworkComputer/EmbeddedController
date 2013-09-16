/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Battery pack info for Kirby
 */

#include "battery_pack.h"

/* Battery temperature ranges in degrees C */
const struct battery_temperature_ranges bat_temp_ranges = {
	.start_charging_min_c = 0,
	.start_charging_max_c = 45,
	.charging_min_c       = 0,
	.charging_max_c       = 60,
	.discharging_min_c    = 0,
	.discharging_max_c    = 100,
};

static const struct battery_info info = {
	/*
	 *   max    = 4.2V
	 *   normal = 3.7V
	 *   min    = 3.0V
	 */
	.voltage_max    = 4200,
	.voltage_normal = 3700,
	.voltage_min    = 3000,

	/* Pre-charge current: I <= 0.01C */
	.precharge_current  = 128, /* mA */
};

const struct battery_info *battery_get_info(void)
{
	return &info;
}
