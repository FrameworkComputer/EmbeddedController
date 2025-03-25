/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "chipset.h"
#include "common.h"
#include "fan.h"
#include "temp_sensor/temp_sensor.h"
#include "thermal.h"
#include "util.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(board_thermal, LOG_LEVEL_INF);

#define TEMP_DDR_SOC TEMP_SENSOR_ID(DT_NODELABEL(temperature_ddr_soc))
#define TEMP_AMBIENT TEMP_SENSOR_ID(DT_NODELABEL(temperature_ambient))
#define TEMP_IMVP_SOC TEMP_SENSOR_ID(DT_NODELABEL(temperature_imvp_soc))
#define TEMP_NVME TEMP_SENSOR_ID(DT_NODELABEL(temperature_nvme))

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
	DT_NODELABEL(fan_steps), FAN_TABLE_ENTRY) };

#define NUM_FAN_LEVELS ARRAY_SIZE(fan_step_table)

int fan_table_to_rpm(int fan, int *temp)
{
	/* current fan level */
	static int current_level;
	/* previous fan level */
	static int prev_level;
	/* previous sensor temperature */
	static int prev_temp[TEMP_SENSOR_COUNT];
	int i, temp_off, temp_on;

	/*
	 * Compare the current and previous temperature, we have
	 * the three paths :
	 *  1. decreasing path. (check the release point)
	 *  2. increasing path. (check the trigger point)
	 *  3. invariant path. (return the current RPM)
	 */
	if (temp[TEMP_IMVP_SOC] < prev_temp[TEMP_IMVP_SOC]) {
		for (i = current_level; i > 0; i--) {
			temp_off = fan_step_table[i].off[TEMP_IMVP_SOC];
			if (temp[TEMP_IMVP_SOC] < temp_off) {
				current_level = i - 1;
			} else {
				break;
			}
		}
	} else if (temp[TEMP_IMVP_SOC] > prev_temp[TEMP_IMVP_SOC]) {
		for (i = current_level; i < NUM_FAN_LEVELS; i++) {
			temp_on = fan_step_table[i].on[TEMP_IMVP_SOC];
			if (temp[TEMP_IMVP_SOC] > temp_on) {
				current_level = i + 1;
			} else {
				break;
			}
		}
	}

	/* Ensure current_level will not exceed existing level */
	current_level = CLAMP(current_level, 0, NUM_FAN_LEVELS - 1);

	if (current_level != prev_level) {
		LOG_INF("temp_imvp_soc: %d, prev_temp_imvp_soc: %d",
			temp[TEMP_IMVP_SOC], prev_temp[TEMP_IMVP_SOC]);
		LOG_INF("current_level: %d", current_level);
	}

	for (i = 0; i < TEMP_SENSOR_COUNT; i++)
		prev_temp[i] = temp[i];

	prev_level = current_level;

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
