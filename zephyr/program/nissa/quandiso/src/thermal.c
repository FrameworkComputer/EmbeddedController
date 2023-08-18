/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "fan.h"
#include "temp_sensor/temp_sensor.h"
#include "thermal.h"
#include "util.h"

#include <ap_power/ap_power_interface.h>

#define TEMP_CPU TEMP_SENSOR_ID(DT_NODELABEL(temp_cpu))
#define TEMP_5V TEMP_SENSOR_ID(DT_NODELABEL(temp_5v_regulator))
#define TEMP_CHARGER TEMP_SENSOR_ID(DT_NODELABEL(temp_charger))

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
	int i;
	/*
	 * Compare the current and previous temperature, we have
	 * the three paths :
	 *  1. decreasing path. (check the release point)
	 *  2. increasing path. (check the trigger point)
	 *  3. invariant path. (return the current RPM)
	 *
	 * Quandiso thermal table V1-1
	 * Increase path judgment: CPU || (5V && Charger)
	 * Decrease path judgment: CPU && 5V && Charger
	 */
	if (temp[TEMP_CPU] < prev_tmp[TEMP_CPU] ||
	    temp[TEMP_5V] < prev_tmp[TEMP_5V] ||
	    temp[TEMP_CHARGER] < prev_tmp[TEMP_CHARGER]) {
		for (i = current_level; i > 0; i--) {
			if (temp[TEMP_CPU] < fan_step_table[i].off[TEMP_CPU] &&
			    temp[TEMP_5V] < fan_step_table[i].off[TEMP_5V] &&
			    temp[TEMP_CHARGER] <
				    fan_step_table[i].off[TEMP_CHARGER]) {
				current_level = i - 1;
			} else
				break;
		}
	} else if (temp[TEMP_CPU] > prev_tmp[TEMP_CPU] ||
		   temp[TEMP_5V] > prev_tmp[TEMP_5V] ||
		   temp[TEMP_CHARGER] > prev_tmp[TEMP_CHARGER]) {
		for (i = current_level; i < ARRAY_SIZE(fan_step_table); i++) {
			if (temp[TEMP_CPU] > fan_step_table[i].on[TEMP_CPU] ||
			    (temp[TEMP_5V] > fan_step_table[i].on[TEMP_5V] &&
			     temp[TEMP_CHARGER] >
				     fan_step_table[i].on[TEMP_CHARGER])) {
				current_level = i + 1;
			} else
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
	if (ap_power_in_state(AP_POWER_STATE_ON)) {
		fan_set_rpm_mode(fan, 1);
		fan_set_rpm_target(fan, fan_table_to_rpm(fan, temp));
	}
}
