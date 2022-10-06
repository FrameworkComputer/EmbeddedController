/* Copyright 2022 The ChromiumOS Authors
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
	/* Fan rpm */
	uint16_t rpm[FAN_CH_COUNT];
};

static const struct fan_step fan_table[] = {
	{
		/* level 0 */
		.on = { -1, 47, -1 },
		.off = { -1, 0, -1 },
		.rpm = { 1900 },
	},
	{
		/* level 1 */
		.on = { -1, 50, -1 },
		.off = { -1, 47, -1 },
		.rpm = { 2400 },
	},
	{
		/* level 2 */
		.on = { -1, 60, -1 },
		.off = { -1, 57, -1 },
		.rpm = { 3000 },
	},
	{
		/* level 3 */
		.on = { -1, 70, -1 },
		.off = { -1, 67, -1 },
		.rpm = { 3500 },
	},
	{
		/* level 4 */
		.on = { -1, 80, -1 },
		.off = { -1, 77, -1 },
		.rpm = { 4000 },
	},
	{
		/* level 5 */
		.on = { -1, 90, -1 },
		.off = { -1, 87, -1 },
		.rpm = { 4500 },
	},
};
const int num_fan_levels = ARRAY_SIZE(fan_table);

int fan_table_to_rpm(int fan, int *temp, enum temp_sensor_id temp_sensor)
{
	/* current fan level */
	static int current_level;
	/* previous fan level */
	static int prev_current_level;
	/* previous sensor temperature */
	static int prev_temp[TEMP_SENSOR_COUNT];
	int i;
	int new_rpm = 0;

	/*
	 * Compare the current and previous temperature, we have
	 * the three paths :
	 *  1. decreasing path. (check the release point)
	 *  2. increasing path. (check the trigger point)
	 *  3. invariant path. (return the current RPM)
	 */
	if (temp[temp_sensor] < prev_temp[temp_sensor]) {
		for (i = current_level; i > 0; i--) {
			if (temp[temp_sensor] < fan_table[i].off[temp_sensor])
				current_level = i - 1;
			else
				break;
		}
	} else if (temp[temp_sensor] > prev_temp[temp_sensor]) {
		for (i = current_level; i < num_fan_levels; i++) {
			if (temp[temp_sensor] >= fan_table[i].on[temp_sensor])
				current_level = i;
			else
				break;
		}
	}
	if (current_level < 0)
		current_level = 0;
	if (current_level >= num_fan_levels)
		current_level = num_fan_levels - 1;

	if (current_level != prev_current_level) {
		CPRINTS("temp: %d, prev_temp: %d", temp[temp_sensor],
			prev_temp[temp_sensor]);
		CPRINTS("current_level: %d", current_level);
	}

	prev_temp[temp_sensor] = temp[temp_sensor];
	prev_current_level = current_level;

	switch (fan) {
	case FAN_CH_0:
		new_rpm = fan_table[current_level].rpm[FAN_CH_0];
		break;
	default:
		break;
	}
	return new_rpm;
}

void board_override_fan_control(int fan, int *temp)
{
	if (chipset_in_state(CHIPSET_STATE_ON)) {
		fan_set_rpm_mode(FAN_CH(fan), 1);
		fan_set_rpm_target(FAN_CH(fan),
				   fan_table_to_rpm(FAN_CH(fan), temp,
						    TEMP_SENSOR_2_CPU_VR));
	} else if (chipset_in_state(CHIPSET_STATE_ANY_SUSPEND)) {
		/* Stop fan when enter S0ix */
		fan_set_rpm_mode(FAN_CH(fan), 1);
		fan_set_rpm_target(FAN_CH(fan), 0);
	}
}
