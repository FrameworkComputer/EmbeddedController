/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "chipset.h"
#include "common.h"
#include "console.h"
#include "extpower.h"
#include "fan.h"
#include "hooks.h"
#include "host_command.h"
#include "temp_sensor.h"
#include "thermal.h"
#include "util.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_THERMAL, outstr)
#define CPRINTS(format, args...) cprints(CC_THERMAL, format, ## args)

struct fan_step {
	/*
	 * Sensor 1~3 trigger point, set -1 if we're not using this
	 * sensor to determine fan speed.
	 */
	int8_t on[TEMP_SENSOR_COUNT];

	/*
	 * Sensor 1~3 trigger point, set -1 if we're not using this
	 * sensor to determine fan speed.
	 */
	int8_t off[TEMP_SENSOR_COUNT];

	/* Fan rpm */
	uint16_t rpm;
};

static const struct fan_step fan_step_table[] = {
	{
		/* level 0 */
		.on = {-1, -1, 36},
		.off = {-1, -1, 99},
		.rpm = 0,
	},
	{
		/* level 1 */
		.on = {-1, -1, 40},
		.off = {-1, -1, 32},
		.rpm = 2244,
	},
	{
		/* level 2 */
		.on = {-1, -1, 45},
		.off = {-1, -1, 35},
		.rpm = 2580,
	},
	{
		/* level 3 */
		.on = {-1, -1, 50},
		.off = {-1, -1, 40},
		.rpm = 2824,
	},
	{
		/* level 4 */
		.on = {-1, -1, 55},
		.off = {-1, -1, 45},
		.rpm = 3120,
	},
	{
		/* level 5 */
		.on = {-1, -1, 60},
		.off = {-1, -1, 50},
		.rpm = 3321,
	},
	{
		/* level 6 */
		.on = {-1, -1, 70},
		.off = {-1, -1, 55},
		.rpm = 3780,
	},
	{
		/* level 7 */
		.on = {-1, -1, 80},
		.off = {-1, -1, 60},
		.rpm = 4330,
	},
	{
		/* level 8 */
		.on = {-1, -1, 99},
		.off = {-1, -1, 74},
		.rpm = 4915,
	},
};

#define NUM_FAN_LEVELS ARRAY_SIZE(fan_step_table)

int fan_table_to_rpm(int fan, int *temp)
{
	static int current_level;
	static int prev_tmp[TEMP_SENSOR_COUNT];
	int i;

	/*
	 * Comopare the current and previous temperature, we have
	 * the three path:
	 * 1. decreasing path. (check the release point)
	 * 2. increasing path. (check the trigger point)
	 * 3. invariant path. (return the current RPM)
	 */
	if (temp[TEMP_SENSOR_CPU] < prev_tmp[TEMP_SENSOR_CPU]) {
		if (temp[TEMP_SENSOR_CPU] <
			fan_step_table[current_level].off[TEMP_SENSOR_CPU])
			current_level = current_level - 1;
	} else if (temp[TEMP_SENSOR_CPU] > prev_tmp[TEMP_SENSOR_CPU]) {
		if (temp[TEMP_SENSOR_CPU] >
			fan_step_table[current_level].on[TEMP_SENSOR_CPU])
			current_level = current_level + 1;
	}

	if (current_level < 0)
		current_level = 0;
	else if (current_level > NUM_FAN_LEVELS)
		current_level = NUM_FAN_LEVELS;

	for (i = 0; i < TEMP_SENSOR_COUNT; i++)
		prev_tmp[i] = temp[i];

	return fan_step_table[current_level].rpm;
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
