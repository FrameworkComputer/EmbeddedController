/* Copyright 2019 The Chromium OS Authors. All rights reserved.
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

static const struct fan_step *fan_step_table;

static const struct fan_step fan_table_clamshell[] = {
	{
		/* level 0 */
		.on = {0, -1, 54, 34},
		.off = {99, -1, 99, 99},
		.rpm = {0, 0},
	},
	{
		/* level 1 */
		.on = {0, -1, 57, 35},
		.off = {99, -1, 54, 34},
		.rpm = {3950, 3850},
	},
	{
		/* level 2 */
		.on = {0, -1, 58, 36},
		.off = {99, -1, 57, 35},
		.rpm = {4200, 4100},
	},
	{
		/* level 3 */
		.on = {0, -1, 59, 37},
		.off = {99, -1, 58, 36},
		.rpm = {4550, 4450},
	},
	{
		/* level 4 */
		.on = {60, -1, 60, 38},
		.off = {58, -1, 59, 37},
		.rpm = {4900, 4800},
	},
	{
		/* level 5 */
		.on = {62, -1, 61, 39},
		.off = {60, -1, 60, 38},
		.rpm = {5250, 5150},
	},
	{
		/* level 6 */
		.on = {65, -1, 64, 40},
		.off = {62, -1, 61, 39},
		.rpm = {5400, 5300},
	},
	{
		/* level 7 */
		.on = {100, -1, 100, 100},
		.off = {65, -1, 62, 40},
		.rpm = {6000, 6150},
	},
};

static const struct fan_step fan_table_tablet[] = {
	{
		/* level 0 */
		.on = {0, -1, 55, 39},
		.off = {99, -1, 99, 99},
		.rpm = {0, 0},
	},
	{
		/* level 1 */
		.on = {0, -1, 56, 40},
		.off = {99, -1, 55, 39},
		.rpm = {0, 0},
	},
	{
		/* level 2 */
		.on = {0, -1, 57, 41},
		.off = {99, -1, 56, 40},
		.rpm = {4000, 3350},
	},
	{
		/* level 3 */
		.on = {0, -1, 58, 42},
		.off = {99, -1, 57, 41},
		.rpm = {4200, 3400},
	},
	{
		/* level 4 */
		.on = {60, -1, 59, 43},
		.off = {58, -1, 58, 42},
		.rpm = {4400, 3500},
	},
	{
		/* level 5 */
		.on = {62, -1, 60, 44},
		.off = {60, -1, 59, 43},
		.rpm = {4800, 4350},
	},
	{
		/* level 6 */
		.on = {65, -1, 61, 45},
		.off = {62, -1, 60, 44},
		.rpm = {5000, 4500},
	},
	{
		/* level 7 */
		.on = {100, -1, 100, 100},
		.off = {65, -1, 61, 45},
		.rpm = {5200, 5100},
	},
};

#define NUM_FAN_LEVELS ARRAY_SIZE(fan_table_clamshell)

BUILD_ASSERT(ARRAY_SIZE(fan_table_clamshell) ==
	ARRAY_SIZE(fan_table_tablet));

int fan_table_to_rpm(int fan, int *temp)
{
	static int current_level;
	static int prev_tmp[TEMP_SENSOR_COUNT];
	static int new_rpm;
	int i;

	if (tablet_get_mode())
		fan_step_table = fan_table_tablet;
	else
		fan_step_table = fan_table_clamshell;

	/*
	 * Compare the current and previous temperature, we have
	 * the three paths :
	 *  1. decreasing path. (check the release point)
	 *  2. increasing path. (check the trigger point)
	 *  3. invariant path. (return the current RPM)
	 */

	if (temp[TEMP_SENSOR_1] < prev_tmp[TEMP_SENSOR_1] ||
		temp[TEMP_SENSOR_3] < prev_tmp[TEMP_SENSOR_3] ||
		temp[TEMP_SENSOR_4] < prev_tmp[TEMP_SENSOR_4]) {
		for (i = current_level; i > 0; i--) {
			if (temp[TEMP_SENSOR_1] < fan_step_table[i].off[TEMP_SENSOR_1] &&
				 temp[TEMP_SENSOR_4] < fan_step_table[i].off[TEMP_SENSOR_4] &&
				 temp[TEMP_SENSOR_3] < fan_step_table[i].off[TEMP_SENSOR_3])
				current_level = i - 1;
			else
				break;
		}
	} else if (temp[TEMP_SENSOR_1] > prev_tmp[TEMP_SENSOR_1] ||
		   temp[TEMP_SENSOR_3] > prev_tmp[TEMP_SENSOR_3] ||
		   temp[TEMP_SENSOR_4] > prev_tmp[TEMP_SENSOR_4]) {
		for (i = current_level; i < NUM_FAN_LEVELS; i++) {
			if ((temp[TEMP_SENSOR_1] > fan_step_table[i].on[TEMP_SENSOR_1] &&
				 temp[TEMP_SENSOR_4] > fan_step_table[i].on[TEMP_SENSOR_4]) ||
				 temp[TEMP_SENSOR_3] > fan_step_table[i].on[TEMP_SENSOR_3])
				current_level = i + 1;
			else
				break;
		}
	}

	if (current_level < 0)
		current_level = 0;

	for (i = 0; i < TEMP_SENSOR_COUNT; ++i)
		prev_tmp[i] = temp[i];

	ASSERT(current_level < NUM_FAN_LEVELS);

	switch (fan) {
	case FAN_CH_0:
		new_rpm = fan_step_table[current_level].rpm[FAN_CH_0];
		break;
	case FAN_CH_1:
		new_rpm = fan_step_table[current_level].rpm[FAN_CH_1];
		break;
	default:
		break;
	}

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
