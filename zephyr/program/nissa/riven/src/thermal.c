/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "chipset.h"
#include "common.h"
#include "cros_cbi.h"
#include "fan.h"
#include "hooks.h"
#include "temp_sensor/temp_sensor.h"
#include "thermal.h"
#include "util.h"

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(nissa, CONFIG_NISSA_LOG_LEVEL);

#define THERMAL_SOLUTION_COUNT 1
#define TEMP_MEMORY TEMP_SENSOR_ID(DT_NODELABEL(temp_memory))
BUILD_ASSERT(TEMP_MEMORY == 0 || TEMP_MEMORY <= THERMAL_SOLUTION_COUNT);

struct fan_step {
	/*
	 * The only sensor temp_memory trigger point
	 */
	int8_t on[THERMAL_SOLUTION_COUNT];
	/*
	 * The only sensor temp_memory trigger point
	 */
	int8_t off[THERMAL_SOLUTION_COUNT];
	/* Fan rpm */
	uint16_t rpm[FAN_CH_COUNT];
};

#define FAN_TABLE_ENTRY(nd)                     \
	{                                       \
		.on = DT_PROP(nd, temp_on),     \
		.off = DT_PROP(nd, temp_off),   \
		.rpm = DT_PROP(nd, rpm_target), \
	},

static const struct fan_step fan_step_table_6w[] = { DT_FOREACH_CHILD(
	DT_NODELABEL(fan_steps_6w), FAN_TABLE_ENTRY) };
static const struct fan_step fan_step_table_15w[] = { DT_FOREACH_CHILD(
	DT_NODELABEL(fan_steps_15w), FAN_TABLE_ENTRY) };

static uint8_t thermal_solution;

int fan_table_to_rpm(int fan, int *temp)
{
	const struct fan_step *fan_step_table;
	/* current fan level */
	static int current_level;
	/* previous sensor temperature */
	static int prev_tmp[THERMAL_SOLUTION_COUNT];

	int i;
	uint8_t fan_table_size;

	/*
	 * thermal table decides the FW_THERMAL flag of fw_config
	 * unset (0): 6W, 1: 6W, 2: 15W table
	 */
	if (thermal_solution == FW_THERMAL_15W) {
		fan_step_table = fan_step_table_15w;
		fan_table_size = ARRAY_SIZE(fan_step_table_15w);
	} else {
		fan_step_table = fan_step_table_6w;
		fan_table_size = ARRAY_SIZE(fan_step_table_6w);
	}

	/*
	 * Compare the current and previous temperature, we have
	 * the three paths :
	 *  1. decreasing path. (check the release point)
	 *  2. increasing path. (check the trigger point)
	 *  3. invariant path. (return the current RPM)
	 */

	if (temp[TEMP_MEMORY] < prev_tmp[TEMP_MEMORY]) {
		for (i = current_level; i > 0; i--) {
			if (temp[TEMP_MEMORY] <=
			    fan_step_table[i].off[TEMP_MEMORY])
				current_level = i - 1;
			else
				break;
		}
	} else if (temp[TEMP_MEMORY] > prev_tmp[TEMP_MEMORY]) {
		for (i = current_level; i < fan_table_size; i++) {
			if (temp[TEMP_MEMORY] >=
			    fan_step_table[i].on[TEMP_MEMORY])
				current_level = i;
			else
				break;
		}
	}

	if (current_level < 0)
		current_level = 0;

	if (current_level >= fan_table_size)
		current_level = fan_table_size - 1;

	prev_tmp[TEMP_MEMORY] = temp[TEMP_MEMORY];

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
	} else if (chipset_in_state(CHIPSET_STATE_ANY_SUSPEND)) {
		/* Stop fan when enter S0ix */
		fan_set_rpm_mode(fan, 1);
		fan_set_rpm_target(fan, 0);
	}
}

test_export_static void thermal_init(void)
{
	int ret;
	uint32_t val;
	/*
	 * Retrieve the fan config.
	 */
	ret = cros_cbi_get_fw_config(FW_THERMAL, &val);
	if (ret != 0) {
		LOG_ERR("Error retrieving CBI FW_CONFIG field %d", FW_THERMAL);
		return;
	}

	thermal_solution = val;
}
DECLARE_HOOK(HOOK_INIT, thermal_init, HOOK_PRIO_POST_FIRST);
