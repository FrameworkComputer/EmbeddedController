/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Quartz Fan configuration */

#include "chipset.h"
#include "common.h"
#include "fan.h"
#include "power/qcom.h"
#include "temp_sensor/temp_sensor.h"
#include "thermal.h"
#include "util.h"

#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(quartz_fan, LOG_LEVEL_INF);

#define TEMP_PMIC TEMP_SENSOR_ID(DT_NODELABEL(temp_pmic))
#define TEMP_SSD TEMP_SENSOR_ID(DT_NODELABEL(temp_ssd))
#define TEMP_CHG TEMP_SENSOR_ID(DT_NODELABEL(temp_charger))

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
	DT_NODELABEL(fan_steps), FAN_TABLE_ENTRY) };

int fan_table_to_rpm(int fan, int *temp)
{
	/* current fan level */
	static int current_level;
	/* previous fan level */
	static int prev_current_level;
	/* previous sensor temperature */
	static int prev_temp[TEMP_SENSOR_COUNT];
	int i;

	if (temp[TEMP_PMIC] < prev_temp[TEMP_PMIC] ||
	    temp[TEMP_SSD] < prev_temp[TEMP_SSD] ||
	    temp[TEMP_CHG] < prev_temp[TEMP_CHG]) {
		for (i = current_level; i > 0; i--) {
			if (temp[TEMP_PMIC] <
				    fan_step_table[i].off[TEMP_PMIC] &&
			    temp[TEMP_SSD] < fan_step_table[i].off[TEMP_SSD] &&
			    temp[TEMP_CHG] < fan_step_table[i].off[TEMP_CHG]) {
				current_level = i - 1;
			} else
				break;
		}
	} else if (temp[TEMP_PMIC] > prev_temp[TEMP_PMIC] ||
		   temp[TEMP_SSD] > prev_temp[TEMP_SSD] ||
		   temp[TEMP_CHG] > prev_temp[TEMP_CHG]) {
		for (i = current_level; i < ARRAY_SIZE(fan_step_table) - 1;
		     i++) {
			if (temp[TEMP_PMIC] >=
				    fan_step_table[i].on[TEMP_PMIC] ||
			    temp[TEMP_SSD] >= fan_step_table[i].on[TEMP_SSD] ||
			    temp[TEMP_CHG] >= fan_step_table[i].on[TEMP_CHG]) {
				current_level = i + 1;
			} else
				break;
		}
	}

	if (current_level < 0 || current_level >= ARRAY_SIZE(fan_step_table))
		current_level = CLAMP(current_level, 0,
				      (ARRAY_SIZE(fan_step_table) - 1));

	if (current_level != prev_current_level) {
		LOG_INF("prev_temp_pmic: %d, prev_temp_ssd: %d, prev_temp_chg: %d",
			prev_temp[TEMP_PMIC], prev_temp[TEMP_SSD],
			prev_temp[TEMP_CHG]);
		LOG_INF("temp_pmic: %d, temp_ssd: %d, temp_chg: %d",
			temp[TEMP_PMIC], temp[TEMP_SSD], temp[TEMP_CHG]);
		LOG_INF("current_level: %d", current_level);
	}

	for (i = 0; i < TEMP_SENSOR_COUNT; ++i)
		prev_temp[i] = temp[i];

	prev_current_level = current_level;

	return fan_step_table[current_level].rpm[fan];
}

/**
 * Override default fan control.
 *
 * On Quartz, Disable Fan control when AP boots for charging.
 */
enum fan_status board_override_fan_control_duty(int ch)
{
	/* If the last power-on is due to AC, which enters the charging loop in
	 * the AP firmware stop the fan */
	if (POWER_ON_BY_AC_ON == chipset_get_power_on_reason()) {
		fan_set_duty(ch, 0);
		return FAN_STATUS_STOPPED;
	}

	return fan_smart_control(ch);
}

void board_override_fan_control(int fan, int *temp)
{
	/*
	 * In common/fan.c pwm_fan_stop() will turn off fan
	 * when chipset suspend or shutdown.
	 */
	if (chipset_in_state(CHIPSET_STATE_ON)) {
		fan_set_rpm_mode(fan, true);
		fan_set_rpm_target(fan, fan_table_to_rpm(fan, temp));
	}
}
