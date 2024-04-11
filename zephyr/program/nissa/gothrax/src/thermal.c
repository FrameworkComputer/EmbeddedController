/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "chipset.h"
#include "common.h"
#include "console.h"
#include "fan.h"
#include "temp_sensor/temp_sensor.h"
#include "thermal.h"
#include "util.h"

#include <ap_power/ap_power_interface.h>

#define CPRINTS(format, args...) cprints(CC_THERMAL, format, ##args)

/*
 * Only consider sensor TEMP_MEMORY to control fan.
 */

#define TEMP_MEMORY TEMP_SENSOR_ID(DT_NODELABEL(temp_memory))

struct fan_step {
	int8_t on[TEMP_SENSOR_COUNT];
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
	/* previous fan level */
	static int prev_current_level;
	/* previous sensor temperature */
	static int prev_tmp[TEMP_SENSOR_COUNT];
	int i;

	if (temp[TEMP_MEMORY] < prev_tmp[TEMP_MEMORY]) {
		for (i = current_level; i > 0; i--) {
			if (temp[TEMP_MEMORY] <
			    fan_step_table[i].off[TEMP_MEMORY]) {
				current_level = i - 1;
			} else
				break;
		}
	} else if (temp[TEMP_MEMORY] > prev_tmp[TEMP_MEMORY]) {
		for (i = current_level; i < ARRAY_SIZE(fan_step_table); i++) {
			if (temp[TEMP_MEMORY] >=
			    fan_step_table[i].on[TEMP_MEMORY]) {
				current_level = i;
			} else
				break;
		}
	}

	if (current_level < 0)
		current_level = 0;

	if (current_level >= ARRAY_SIZE(fan_step_table))
		current_level = ARRAY_SIZE(fan_step_table) - 1;

	if (current_level != prev_current_level) {
		CPRINTS("temp: %d, prev_temp: %d", temp[TEMP_MEMORY],
			prev_tmp[TEMP_MEMORY]);
		CPRINTS("current_level: %d", current_level);
	}

	for (i = 0; i < TEMP_SENSOR_COUNT; ++i)
		prev_tmp[i] = temp[i];
	prev_current_level = current_level;

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
