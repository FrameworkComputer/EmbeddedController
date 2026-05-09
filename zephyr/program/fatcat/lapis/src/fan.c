/* Copyright 2025 The ChromiumOS Authors
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

#define CPRINTS(format, args...) cprints(CC_THERMAL, format, ##args)

#define TEMP_VR TEMP_SENSOR_ID(DT_NODELABEL(temp_vr))
#define TEMP_DDR TEMP_SENSOR_ID(DT_NODELABEL(temp_ddr))
#define TEMP_TOP TEMP_SENSOR_ID(DT_NODELABEL(temp_top))
#define TEMP_SSD TEMP_SENSOR_ID(DT_NODELABEL(temp_ssd))
#define TEMP_CHG TEMP_SENSOR_ID(DT_NODELABEL(temp_charger))

struct fan_step {
	int on[TEMP_SENSOR_COUNT];
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
	static int prev_tmp[TEMP_SENSOR_COUNT];
	int i;

	/*
	 * Compare the current and previous temperature, we have
	 * the three paths :
	 *  1. increasing path. (check the trigger point)
	 *  2. decreasing path. (check the release point)
	 *  3. invariant path. (return the current RPM)
	 */
	if ((temp[TEMP_VR] > prev_tmp[TEMP_VR]) ||
	    (temp[TEMP_DDR] > prev_tmp[TEMP_DDR]) ||
	    (temp[TEMP_TOP] > prev_tmp[TEMP_TOP]) ||
	    (temp[TEMP_SSD] > prev_tmp[TEMP_SSD]) ||
	    (temp[TEMP_CHG] > prev_tmp[TEMP_CHG])) {
		if ((temp[TEMP_VR] >=
		     fan_step_table[current_level].on[TEMP_VR]) ||
		    (temp[TEMP_DDR] >=
		     fan_step_table[current_level].on[TEMP_DDR]) ||
		    (temp[TEMP_TOP] >=
		     fan_step_table[current_level].on[TEMP_TOP]) ||
		    (temp[TEMP_SSD] >=
		     fan_step_table[current_level].on[TEMP_SSD]) ||
		    (temp[TEMP_CHG] >=
		     fan_step_table[current_level].on[TEMP_CHG]))
			current_level++;
	} else if ((temp[TEMP_VR] < prev_tmp[TEMP_VR]) ||
		   (temp[TEMP_DDR] < prev_tmp[TEMP_DDR]) ||
		   (temp[TEMP_TOP] < prev_tmp[TEMP_TOP]) ||
		   (temp[TEMP_SSD] < prev_tmp[TEMP_SSD]) ||
		   (temp[TEMP_CHG] < prev_tmp[TEMP_CHG])) {
		if ((temp[TEMP_VR] <=
		     fan_step_table[current_level].off[TEMP_VR]) &&
		    (temp[TEMP_DDR] <=
		     fan_step_table[current_level].off[TEMP_DDR]) &&
		    (temp[TEMP_TOP] <=
		     fan_step_table[current_level].off[TEMP_TOP]) &&
		    (temp[TEMP_SSD] <=
		     fan_step_table[current_level].off[TEMP_SSD]) &&
		    (temp[TEMP_CHG] <=
		     fan_step_table[current_level].off[TEMP_CHG]))
			current_level--;
	}

	/* Keep fan level stay in fan steps */
	if (current_level < 0 || current_level >= NUM_FAN_LEVELS)
		current_level = CLAMP(current_level, 0, (NUM_FAN_LEVELS - 1));

	if (current_level != prev_level) {
		CPRINTS("fan_level: %d", current_level);
	}
	prev_level = current_level;

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
