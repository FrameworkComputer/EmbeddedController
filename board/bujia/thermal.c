/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "chipset.h"
#include "common.h"
#include "console.h"
#include "fan.h"
#include "hooks.h"
#include "host_command.h"
#include "temp_sensor.h"
#include "thermal.h"
#include "util.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_THERMAL, outstr)
#define CPRINTS(format, args...) cprints(CC_THERMAL, format, ##args)

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

static const struct fan_step fan_table[] = {
	{
		/* level 0 */
		.on = { 50, 57, -1, 0 },
		.off = { 99, 99, -1, 99 },
		.rpm = { 0 },
	},
	{
		/* level 1 */
		.on = { 60, 67, -1, 52 },
		.off = { 45, 52, -1, 99 },
		.rpm = { 2800 },
	},
	{
		/* level 2 */
		.on = { 70, 77, -1, 58 },
		.off = { 55, 62, -1, 49 },
		.rpm = { 3600 },
	},
	{
		/* level 3 */
		.on = { 99, 99, -1, 99 },
		.off = { 65, 72, -1, 55 },
		.rpm = { 4000 },
	},
};

#define NUM_FAN_LEVELS ARRAY_SIZE(fan_table)

int fan_table_to_rpm(int fan, int *temp)
{
	static int current_level;
	static int prev_tmp[TEMP_SENSOR_COUNT];
	int i;

	/*
	 * Compare the current and previous temperature, we have
	 * the three paths :
	 *  1. decreasing path. (check the release point)
	 *  2. increasing path. (check the trigger point)
	 *  3. invariant path. (return the current RPM)
	 */
	if (temp[TEMP_SENSOR_1_CPU] < prev_tmp[TEMP_SENSOR_1_CPU] ||
	    temp[TEMP_SENSOR_2_CPU_VR] < prev_tmp[TEMP_SENSOR_2_CPU_VR] ||
	    temp[TEMP_SENSOR_4_DIMM] < prev_tmp[TEMP_SENSOR_4_DIMM]) {
		for (i = current_level; i > 0; i--) {
			if (temp[TEMP_SENSOR_2_CPU_VR] <
				    fan_table[i].off[TEMP_SENSOR_2_CPU_VR] &&
			    temp[TEMP_SENSOR_4_DIMM] <
				    fan_table[i].off[TEMP_SENSOR_4_DIMM] &&
			    temp[TEMP_SENSOR_1_CPU] <
				    fan_table[i].off[TEMP_SENSOR_1_CPU])
				current_level = i - 1;
			else
				break;
		}
	} else if (temp[TEMP_SENSOR_1_CPU] > prev_tmp[TEMP_SENSOR_1_CPU] ||
		   temp[TEMP_SENSOR_2_CPU_VR] >
			   prev_tmp[TEMP_SENSOR_2_CPU_VR] ||
		   temp[TEMP_SENSOR_4_DIMM] > prev_tmp[TEMP_SENSOR_4_DIMM]) {
		for (i = current_level; i < NUM_FAN_LEVELS; i++) {
			if ((temp[TEMP_SENSOR_2_CPU_VR] >
				     fan_table[i].on[TEMP_SENSOR_2_CPU_VR] &&
			     temp[TEMP_SENSOR_4_DIMM] >
				     fan_table[i].on[TEMP_SENSOR_4_DIMM]) ||
			    temp[TEMP_SENSOR_1_CPU] >
				    fan_table[i].on[TEMP_SENSOR_1_CPU])
				current_level = i + 1;
			else
				break;
		}
	}

	if (current_level < 0)
		current_level = 0;

	if (current_level >= NUM_FAN_LEVELS)
		current_level = NUM_FAN_LEVELS - 1;

	for (i = 0; i < TEMP_SENSOR_COUNT; ++i)
		prev_tmp[i] = temp[i];

	return fan_table[current_level].rpm[FAN_CH_0];
}

void board_override_fan_control(int fan, int *tmp)
{
	if (chipset_in_state(CHIPSET_STATE_ON | CHIPSET_STATE_ANY_SUSPEND)) {
		fan_set_rpm_mode(FAN_CH(fan), 1);
		fan_set_rpm_target(FAN_CH(fan), fan_table_to_rpm(fan, tmp));
	}
}
