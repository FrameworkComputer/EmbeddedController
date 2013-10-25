/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Battery pack vendor provided charging profile
 */

#include "battery.h"
#include "gpio.h"

const struct battery_temperature_ranges bat_temp_ranges = {
	.start_charging_min_c = 0,
	.start_charging_max_c = 45,
	.charging_min_c       = 0,
	.charging_max_c       = 45,
	.discharging_min_c    = 0,
	.discharging_max_c    = 60,
};

static const struct battery_info info = {
	.voltage_max    = 8400,		/* mV */
	.voltage_normal = 7400,
	.voltage_min    = 6000,
	.precharge_current  = 256,	/* mA */
};

const struct battery_info *battery_get_info(void)
{
	return &info;
}

/* TODO(crosbug.com/p/23597): Battery cutoff command; need vendor info */
