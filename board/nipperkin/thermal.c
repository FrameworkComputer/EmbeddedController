/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "chipset.h"
#include "common.h"
#include "console.h"
#include "fan.h"
#include "hooks.h"
#include "host_command.h"
#include "tablet_mode.h"
#include "temp_sensor.h"
#include "thermal.h"
#include "util.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_THERMAL, outstr)
#define CPRINTS(format, args...) cprints(CC_THERMAL, format, ## args)

struct fan_step {
	/*
	 * Sensor 1~4 trigger point, set -1 if we're not using this
	 * sensor to determine fan speed.
	 */
	int8_t on[TEMP_SENSOR_COUNT];

	/*
	 * Sensor 1~4 trigger point, set -1 if we're not using this
	 * sensor to determine fan speed.
	 */
	int8_t off[TEMP_SENSOR_COUNT];

	/* Fan 1~2 rpm */
	uint16_t rpm[FAN_CH_COUNT];
};

static const struct fan_step fan_step_table[] = {
	{
		/* level 0 */
		.on = {-1, 0, 49, -1, -1, -1},
		.off = {-1, 99, 99, -1, -1, -1},
		.rpm = {0},
	},
	{
		/* level 1 */
		.on = {-1, 0, 50, -1, -1, -1},
		.off = {-1, 99, 48, -1, -1, -1},
		.rpm = {3000},
	},
	{
		/* level 2 */
		.on = {-1, 0, 51, -1, -1, -1},
		.off = {-1, 99, 49, -1, -1, -1},
		.rpm = {3200},
	},
	{
		/* level 3 */
		.on = {-1, 0, 52, -1, -1, -1},
		.off = {-1, 99, 50, -1, -1, -1},
		.rpm = {3600},
	},
	{
		/* level 4 */
		.on = {-1, 50, 54, -1, -1, -1},
		.off = {-1, 47, 51, -1, -1, -1},
		.rpm = {3900},
	},
	{
		/* level 5 */
		.on = {-1, 52, 56, -1, -1, -1},
		.off = {-1, 49, 53, -1, -1, -1},
		.rpm = {4200},
	},
	{
		/* level 6 */
		.on = {-1, 100, 100, -1, -1, -1},
		.off = {-1, 51, 55, -1, -1, -1},
		.rpm = {4600},
	},
};

#define NUM_FAN_LEVELS ARRAY_SIZE(fan_step_table)

BUILD_ASSERT(ARRAY_SIZE(fan_step_table) ==
	ARRAY_SIZE(fan_step_table));

int fan_table_to_rpm(int fan, int *temp)
{
	static int current_level;
	static int prev_tmp[TEMP_SENSOR_COUNT];
	int new_rpm = 0;
	int i;

	/*
	 * Compare the current and previous temperature, we have
	 * the three paths :
	 *  1. decreasing path. (check the release point)
	 *  2. increasing path. (check the trigger point)
	 *  3. invariant path. (return the current RPM)
	 */
	if (temp[TEMP_SENSOR_CHARGER] < prev_tmp[TEMP_SENSOR_CHARGER] ||
	    temp[TEMP_SENSOR_MEMORY] < prev_tmp[TEMP_SENSOR_MEMORY]) {
		for (i = current_level; i > 0; i--) {
			if (temp[TEMP_SENSOR_CHARGER] <
				fan_step_table[i].off[TEMP_SENSOR_CHARGER] &&
			    temp[TEMP_SENSOR_MEMORY] <
				fan_step_table[i].off[TEMP_SENSOR_MEMORY]) {
				current_level = i - 1;
			} else
				break;
		}
	} else if (temp[TEMP_SENSOR_CHARGER] > prev_tmp[TEMP_SENSOR_CHARGER] ||
		   temp[TEMP_SENSOR_MEMORY] > prev_tmp[TEMP_SENSOR_MEMORY]) {
		for (i = current_level; i < NUM_FAN_LEVELS; i++) {
			if ((temp[TEMP_SENSOR_CHARGER] >
				fan_step_table[i].on[TEMP_SENSOR_CHARGER] &&
			    temp[TEMP_SENSOR_MEMORY] >
				fan_step_table[i].on[TEMP_SENSOR_MEMORY])) {
				current_level = i + 1;
			} else
				break;
		}
	}

	if (current_level < 0)
		current_level = 0;

	if (current_level >= NUM_FAN_LEVELS)
		current_level = NUM_FAN_LEVELS - 1;

	for (i = 0; i < TEMP_SENSOR_COUNT; ++i)
		prev_tmp[i] = temp[i];

	new_rpm = fan_step_table[current_level].rpm[FAN_CH_0];

	return new_rpm;
}

void board_override_fan_control(int fan, int *tmp)
{
	if (chipset_in_state(CHIPSET_STATE_ON |
		CHIPSET_STATE_ANY_SUSPEND)) {
		fan_set_rpm_mode(FAN_CH(fan), 1);
		fan_set_rpm_target(FAN_CH(fan),
			fan_table_to_rpm(fan, tmp));
	}
}
