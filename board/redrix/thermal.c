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

static const struct fan_step *fan_step_table;

static const struct fan_step fan_table_clamshell[] = {
	{
		/* level 0 */
		.on = {44, 46, 0, -1},
		.off = {99, 99, 99, -1},
		.rpm = {0, 0},
	},
	{
		/* level 1 */
		.on = {45, 47, 0, -1},
		.off = {44, 46, 99, -1},
		.rpm = {4000, 4400},
	},
	{
		/* level 2 */
		.on = {46, 48, 0, -1},
		.off = {45, 47, 99, -1},
		.rpm = {4700, 5000},
	},
	{
		/* level 3 */
		.on = {47, 49, 0, -1},
		.off = {46, 48, 99, -1},
		.rpm = {5000, 5400},
	},
	{
		/* level 4 */
		.on = {48, 50, 50, -1},
		.off = {47, 49, 48, -1},
		.rpm = {5300, 5600},
	},
	{
		/* level 5 */
		.on = {49, 51, 52, -1},
		.off = {48, 50, 50, -1},
		.rpm = {5700, 6000},
	},
	{
		/* level 6 */
		.on = {100, 100, 100, -1},
		.off = {49, 51, 52, -1},
		.rpm = {6200, 6400},
	},
};

static const struct fan_step fan_table_tablet[] = {
	{
		/* level 0 */
		.on = {44, 46, 0, -1},
		.off = {99, 99, 99, -1},
		.rpm = {0, 0},
	},
	{
		/* level 1 */
		.on = {45, 47, 0, -1},
		.off = {44, 46, 99, -1},
		.rpm = {4200, 4400},
	},
	{
		/* level 2 */
		.on = {46, 48, 0, -1},
		.off = {45, 47, 99, -1},
		.rpm = {4700, 4900},
	},
	{
		/* level 3 */
		.on = {47, 49, 0, -1},
		.off = {46, 48, 99, -1},
		.rpm = {5000, 5300},
	},
	{
		/* level 4 */
		.on = {48, 50, 50, -1},
		.off = {47, 49, 48, -1},
		.rpm = {5200, 5500},
	},
	{
		/* level 5 */
		.on = {49, 51, 52, -1},
		.off = {48, 50, 50, -1},
		.rpm = {5700, 5900},
	},
	{
		/* level 6 */
		.on = {100, 100, 100, -1},
		.off = {49, 51, 52, -1},
		.rpm = {6100, 6300},
	},
};

#define NUM_FAN_LEVELS ARRAY_SIZE(fan_table_clamshell)

BUILD_ASSERT(ARRAY_SIZE(fan_table_clamshell) ==
	ARRAY_SIZE(fan_table_tablet));

int fan_table_to_rpm(int fan, int *temp)
{
	static int current_level;
	static int prev_tmp[TEMP_SENSOR_COUNT];
	int new_rpm = 0;
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
	if (temp[TEMP_SENSOR_1_DDR] < prev_tmp[TEMP_SENSOR_1_DDR] ||
	    temp[TEMP_SENSOR_2_SOC] < prev_tmp[TEMP_SENSOR_2_SOC] ||
	    temp[TEMP_SENSOR_3_CHARGER] < prev_tmp[TEMP_SENSOR_3_CHARGER]) {
		for (i = current_level; i > 0; i--) {
			if (temp[TEMP_SENSOR_1_DDR] <
				fan_step_table[i].off[TEMP_SENSOR_1_DDR] &&
			    temp[TEMP_SENSOR_3_CHARGER] <
				fan_step_table[i].off[TEMP_SENSOR_3_CHARGER] &&
			    temp[TEMP_SENSOR_2_SOC] <
				fan_step_table[i].off[TEMP_SENSOR_2_SOC])
				current_level = i - 1;
			else
				break;
		}
	} else if (temp[TEMP_SENSOR_1_DDR] > prev_tmp[TEMP_SENSOR_1_DDR] ||
		   temp[TEMP_SENSOR_2_SOC] > prev_tmp[TEMP_SENSOR_2_SOC] ||
		   temp[TEMP_SENSOR_3_CHARGER] >
			prev_tmp[TEMP_SENSOR_3_CHARGER]) {
		for (i = current_level; i < NUM_FAN_LEVELS; i++) {
			if ((temp[TEMP_SENSOR_1_DDR] >
				fan_step_table[i].on[TEMP_SENSOR_1_DDR] &&
			    temp[TEMP_SENSOR_3_CHARGER] >
				fan_step_table[i].on[TEMP_SENSOR_3_CHARGER]) ||
			    temp[TEMP_SENSOR_2_SOC] >
				fan_step_table[i].on[TEMP_SENSOR_2_SOC])
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
