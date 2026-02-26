/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "chipset.h"
#include "common.h"
#include "fan.h"
#include "temp_sensor/temp_sensor.h"
#include "thermal.h"
#include "util.h"

#include <ap_power/ap_power_interface.h>

#define TEMP_IO_PORT TEMP_SENSOR_ID(DT_NODELABEL(temp_io_port))
#define TEMP_CPU TEMP_SENSOR_ID(DT_NODELABEL(temp_cpu))
#define TEMP_BOARD TEMP_SENSOR_ID(DT_NODELABEL(temp_board))

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
	uint16_t rpm[FAN_CH_COUNT];
};

#define FAN_TABLE_ENTRY(nd)                     \
	{                                       \
		.on = DT_PROP(nd, temp_on),     \
		.off = DT_PROP(nd, temp_off),   \
		.rpm = DT_PROP(nd, rpm_target), \
	},

static const struct fan_step fan_step_table[] = { DT_FOREACH_CHILD(
	DT_INST(0, cros_ec_fan_steps), FAN_TABLE_ENTRY) };

int fan_table_to_rpm(int fan, int *temp)
{
	/* current fan level */
	static int current_level;
	/* previous sensor temperature */
	static int prev_tmp[TEMP_SENSOR_COUNT];
	int i, j;
	int level_change[TEMP_SENSOR_COUNT];
	/*
	 * Compare the current and previous temperature, we have
	 * the three paths :
	 *  1. decreasing path. (check the release point)
	 *  2. increasing path. (check the trigger point)
	 *  3. invariant path. (return the current RPM)
	 *
	 * thermal table V1-1
	 * Increase path judgment: CPU || IO_PORT || BOARD
	 * Decrease path judgment: CPU && IO_PORT && BOARD
	 */
	if (temp[TEMP_IO_PORT] < prev_tmp[TEMP_IO_PORT] ||
	    temp[TEMP_CPU] < prev_tmp[TEMP_CPU] ||
	    temp[TEMP_BOARD] < prev_tmp[TEMP_BOARD]) {
		for (i = current_level; i > 0; i--) {
			for (j = 0; j < TEMP_SENSOR_COUNT; j++) {
				level_change[j] = 0;
				if (fan_step_table[i].off[j] == -1)
					level_change[j] = 1;
			}
			if (temp[TEMP_IO_PORT] <=
				    fan_step_table[i].off[TEMP_IO_PORT] &&
			    fan_step_table[i].off[TEMP_IO_PORT] != -1)
				level_change[TEMP_IO_PORT] = 1;
			if (temp[TEMP_CPU] <= fan_step_table[i].off[TEMP_CPU] &&
			    fan_step_table[i].off[TEMP_CPU] != -1)
				level_change[TEMP_CPU] = 1;
			if (temp[TEMP_BOARD] <=
				    fan_step_table[i].off[TEMP_BOARD] &&
			    fan_step_table[i].off[TEMP_BOARD] != -1)
				level_change[TEMP_BOARD] = 1;

			if (level_change[TEMP_IO_PORT] &&
			    level_change[TEMP_CPU] && level_change[TEMP_BOARD])
				current_level = i - 1;
			else
				break;
		}
	} else if (temp[TEMP_IO_PORT] > prev_tmp[TEMP_IO_PORT] ||
		   temp[TEMP_CPU] > prev_tmp[TEMP_CPU] ||
		   temp[TEMP_BOARD] > prev_tmp[TEMP_BOARD]) {
		for (i = current_level; i < ARRAY_SIZE(fan_step_table); i++) {
			for (j = 0; j < TEMP_SENSOR_COUNT; j++)
				level_change[j] = 0;
			if (temp[TEMP_IO_PORT] >=
				    fan_step_table[i].on[TEMP_IO_PORT] &&
			    fan_step_table[i].on[TEMP_IO_PORT] != -1)
				level_change[TEMP_IO_PORT] = 1;
			if (temp[TEMP_CPU] >= fan_step_table[i].on[TEMP_CPU] &&
			    fan_step_table[i].on[TEMP_CPU] != -1)
				level_change[TEMP_CPU] = 1;
			if (temp[TEMP_BOARD] >=
				    fan_step_table[i].on[TEMP_BOARD] &&
			    fan_step_table[i].on[TEMP_BOARD] != -1)
				level_change[TEMP_BOARD] = 1;

			if (level_change[TEMP_IO_PORT] ||
			    level_change[TEMP_CPU] || level_change[TEMP_BOARD])
				current_level = i + 1;
			else
				break;
		}
	}
	if (current_level < 0)
		current_level = 0;

	if (current_level >= ARRAY_SIZE(fan_step_table))
		current_level = ARRAY_SIZE(fan_step_table) - 1;

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
