/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Guybrush board-specific configuration */

#include "console.h"
#include "fan.h"
#include "thermal.h"
#include "util.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_THERMAL, outstr)
#define CPRINTS(format, args...) cprints(CC_THERMAL, format, ## args)

const struct fan_conf fan_conf_0 = {
	.flags = FAN_USE_RPM_MODE,
	.ch = MFT_CH_0, /* Use MFT id to control fan */
	.pgood_gpio = GPIO_S0_PGOOD,
	.enable_gpio = -1,
};
const struct fan_rpm fan_rpm_0 = {
	.rpm_min = 3000,
	.rpm_start = 3000,
	.rpm_max = 6000,
};
const struct fan_t fans[] = {
	[FAN_CH_0] = {
		.conf = &fan_conf_0,
		.rpm = &fan_rpm_0,
	},
};
BUILD_ASSERT(ARRAY_SIZE(fans) == FAN_CH_COUNT);

struct ec_thermal_config thermal_params[TEMP_SENSOR_COUNT] = {
	[TEMP_SENSOR_SOC] = {
		.temp_host = {
			[EC_TEMP_THRESH_HIGH] = C_TO_K(80),
			[EC_TEMP_THRESH_HALT] = C_TO_K(85),
		},
		.temp_host_release = {
			[EC_TEMP_THRESH_HIGH] = C_TO_K(75),
		},
		.temp_fan_off = C_TO_K(27),
		.temp_fan_max = C_TO_K(80),
	},
	[TEMP_SENSOR_CHARGER] = {
		.temp_host = {
			[EC_TEMP_THRESH_HIGH] = C_TO_K(80),
			[EC_TEMP_THRESH_HALT] = C_TO_K(85),
		},
		.temp_host_release = {
			[EC_TEMP_THRESH_HIGH] = C_TO_K(75),
		},
		.temp_fan_off = 0,
		.temp_fan_max = 0,
	},
	[TEMP_SENSOR_MEMORY] = {
		.temp_host = {
			[EC_TEMP_THRESH_HIGH] = C_TO_K(80),
			[EC_TEMP_THRESH_HALT] = C_TO_K(85),
		},
		.temp_host_release = {
			[EC_TEMP_THRESH_HIGH] = C_TO_K(75),
		},
		.temp_fan_off = 0,
		.temp_fan_max = 0,
	},
	[TEMP_SENSOR_CPU] = {
		.temp_host = {
			[EC_TEMP_THRESH_HIGH] = 0,
			[EC_TEMP_THRESH_HALT] = 0,
		},
		.temp_host_release = {
			[EC_TEMP_THRESH_HIGH] = 0,
		},
		.temp_fan_off = 0,
		.temp_fan_max = 0,
	},
	/*
	 * Note: Leave ambient entries at 0, both as it does not represent a
	 * hotspot and as not all boards have this sensor
	 */
};
BUILD_ASSERT(ARRAY_SIZE(thermal_params) == TEMP_SENSOR_COUNT);

struct fan_step {
	int on;
	int off;
	int rpm;
};

static const struct fan_step fan_table[] = {
	{.on =  0, .off =  1, .rpm = 0},
	{.on =  6, .off =  2, .rpm = 3000},
	{.on = 28, .off = 15, .rpm = 3300},
	{.on = 34, .off = 26, .rpm = 3700},
	{.on = 39, .off = 32, .rpm = 4000},
	{.on = 45, .off = 38, .rpm = 4300},
	{.on = 51, .off = 43, .rpm = 4700},
	{.on = 74, .off = 62, .rpm = 5400},
};
#define NUM_FAN_LEVELS ARRAY_SIZE(fan_table)

int fan_percent_to_rpm(int fan, int pct)
{
	static int current_level;
	static int previous_pct;
	int i;

	/*
	 * Compare the pct and previous pct, we have the three paths :
	 *  1. decreasing path. (check the off point)
	 *  2. increasing path. (check the on point)
	 *  3. invariant path. (return the current RPM)
	 */
	if (pct < previous_pct) {
		for (i = current_level; i >= 0; i--) {
			if (pct <= fan_table[i].off)
				current_level = i - 1;
			else
				break;
		}
	} else if (pct > previous_pct) {
		for (i = current_level + 1; i < NUM_FAN_LEVELS; i++) {
			if (pct >= fan_table[i].on)
				current_level = i;
			else
				break;
		}
	}

	if (current_level < 0)
		current_level = 0;

	previous_pct = pct;

	if (fan_table[current_level].rpm !=
		fan_get_rpm_target(FAN_CH(fan)))
		CPRINTS("Setting fan RPM to %d",
			fan_table[current_level].rpm);

	return fan_table[current_level].rpm;
}
