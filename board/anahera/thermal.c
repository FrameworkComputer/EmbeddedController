/* Copyright 2021 The ChromiumOS Authors
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
		.on = { 53, 51, 0, -1 },
		.off = { 99, 99, 99, -1 },
		.rpm = { 0 },
	},
	{
		/* level 1 */
		.on = { 54, 52, 0, -1 },
		.off = { 52, 50, 99, -1 },
		.rpm = { 3000 },
	},
	{
		/* level 2 */
		.on = { 55, 53, 0, -1 },
		.off = { 53, 51, 99, -1 },
		.rpm = { 3400 },
	},
	{
		/* level 3 */
		.on = { 56, 54, 0, -1 },
		.off = { 54, 52, 99, -1 },
		.rpm = { 3800 },
	},
	{
		/* level 4 */
		.on = { 57, 55, 54, -1 },
		.off = { 55, 53, 51, -1 },
		.rpm = { 4100 },
	},
	{
		/* level 5 */
		.on = { 58, 56, 60, -1 },
		.off = { 56, 54, 52, -1 },
		.rpm = { 4400 },
	},
	{
		/* level 6 */
		.on = { 100, 100, 100, -1 },
		.off = { 57, 59, 58, -1 },
		.rpm = { 4900 },
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
	if (temp[TEMP_SENSOR_1_FAN] < prev_tmp[TEMP_SENSOR_1_FAN] ||
	    temp[TEMP_SENSOR_2_SOC] < prev_tmp[TEMP_SENSOR_2_SOC] ||
	    temp[TEMP_SENSOR_3_CHARGER] < prev_tmp[TEMP_SENSOR_3_CHARGER]) {
		for (i = current_level; i > 0; i--) {
			if (temp[TEMP_SENSOR_1_FAN] <
				    fan_table[i].off[TEMP_SENSOR_1_FAN] &&
			    temp[TEMP_SENSOR_3_CHARGER] <
				    fan_table[i].off[TEMP_SENSOR_3_CHARGER] &&
			    temp[TEMP_SENSOR_2_SOC] <
				    fan_table[i].off[TEMP_SENSOR_2_SOC])
				current_level = i - 1;
			else
				break;
		}
	} else if (temp[TEMP_SENSOR_1_FAN] > prev_tmp[TEMP_SENSOR_1_FAN] ||
		   temp[TEMP_SENSOR_2_SOC] > prev_tmp[TEMP_SENSOR_2_SOC] ||
		   temp[TEMP_SENSOR_3_CHARGER] >
			   prev_tmp[TEMP_SENSOR_3_CHARGER]) {
		for (i = current_level; i < NUM_FAN_LEVELS; i++) {
			if ((temp[TEMP_SENSOR_1_FAN] >
				     fan_table[i].on[TEMP_SENSOR_1_FAN] &&
			     temp[TEMP_SENSOR_3_CHARGER] >
				     fan_table[i].on[TEMP_SENSOR_3_CHARGER]) ||
			    temp[TEMP_SENSOR_2_SOC] >
				    fan_table[i].on[TEMP_SENSOR_2_SOC])
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
