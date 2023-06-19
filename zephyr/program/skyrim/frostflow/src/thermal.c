/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "chipset.h"
#include "common.h"
#include "console.h"
#include "fan.h"
#include "tablet_mode.h"
#include "temp_sensor/temp_sensor.h"
#include "thermal.h"
#include "util.h"

#define TEMP_MEM TEMP_SENSOR_ID(DT_NODELABEL(temp_sensor_memory))

struct fan_step {
	/*
	 * Sensor 0~4 trigger point, set -1 if we're not using this
	 * sensor to determine fan speed.
	 */
	int on[TEMP_SENSOR_COUNT];
	/*
	 * Sensor 0~4 release point, set -1 if we're not using this
	 * sensor to determine fan speed.
	 */
	int off[TEMP_SENSOR_COUNT];
	/* Fan rpm */
	uint16_t rpm[FAN_CH_COUNT];
};

#define FAN_TABLE_ENTRY(nd)                     \
	{                                       \
		.on = DT_PROP(nd, temp_on),     \
		.off = DT_PROP(nd, temp_off),   \
		.rpm = DT_PROP(nd, rpm_target), \
	},

static const struct fan_step fan_table_clamshell[] = { DT_FOREACH_CHILD(
	DT_NODELABEL(fan_steps_clamshell), FAN_TABLE_ENTRY) };

static const struct fan_step fan_table_tablet[] = { DT_FOREACH_CHILD(
	DT_NODELABEL(fan_steps_tablet), FAN_TABLE_ENTRY) };

static const struct fan_step *fan_step_table;
#define NUM_FAN_LEVELS ARRAY_SIZE(fan_table_clamshell)

BUILD_ASSERT(ARRAY_SIZE(fan_table_clamshell) == ARRAY_SIZE(fan_table_tablet));

int fan_table_to_rpm(int fan, int *temp)
{
	/* current fan level */
	static int current_level;
	/* previous sensor temperature */
	static int prev_tmp[TEMP_SENSOR_COUNT];
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

	if (temp[TEMP_MEM] < prev_tmp[TEMP_MEM]) {
		for (i = current_level; i > 0; i--) {
			if (temp[TEMP_MEM] <= fan_step_table[i].off[TEMP_MEM])
				current_level = i - 1;
			else
				break;
		}
	} else if (temp[TEMP_MEM] > prev_tmp[TEMP_MEM]) {
		for (i = current_level; i < NUM_FAN_LEVELS; i++) {
			if (temp[TEMP_MEM] >= fan_step_table[i].on[TEMP_MEM])
				current_level = i;
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

	return fan_step_table[current_level].rpm[fan];
}

void board_override_fan_control(int fan, int *temp)
{
	/*
	 * In common/fan.c pwm_fan_stop() will turn off fan
	 * when chipset suspend or shutdown.
	 */
	if (chipset_in_state(CHIPSET_STATE_ON)) {
		fan_set_rpm_mode(fan, 1);
		fan_set_rpm_target(fan, fan_table_to_rpm(fan, temp));
	}
}
