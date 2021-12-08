/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "battery_smart.h"
#include "charge_state.h"
#include "chipset.h"
#include "common.h"
#include "console.h"
#include "fan.h"
#include "hooks.h"
#include "host_command.h"
#include "tablet_mode.h"
#include "temp_sensor.h"
#include "thermal.h"
#include "util.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_THERMAL, outstr)
#define CPRINTS(format, args...) cprints(CC_THERMAL, format, ## args)

const struct fan_conf fan_conf_0 = {
	.flags = FAN_USE_RPM_MODE,
	.ch = MFT_CH_0, /* Use MFT id to control fan */
	.pgood_gpio = GPIO_S0_PGOOD,
	.enable_gpio = -1,
};
const struct fan_rpm fan_rpm_0 = {
	.rpm_min = 1000,
	.rpm_start = 1000,
	.rpm_max = 6500,
};
const struct fan_t fans[] = {
	[FAN_CH_0] = {
		.conf = &fan_conf_0,
		.rpm = &fan_rpm_0,
	},
};
BUILD_ASSERT(ARRAY_SIZE(fans) == FAN_CH_COUNT);

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

static const struct fan_step fan_step_table[] = {
	{
		/* level 0 */
		.on = {51, 0, 44, -1, -1, -1},
		.off = {99, 99, 99, -1, -1, -1},
		.rpm = {0},
	},
	{
		/* level 1 */
		.on = {52, 0, 47, -1, -1, -1},
		.off = {50, 99, 43, -1, -1, -1},
		.rpm = {3000},
	},
	{
		/* level 2 */
		.on = {53, 0, 49, -1, -1, -1},
		.off = {51, 99, 45, -1, -1, -1},
		.rpm = {3400},
	},
	{
		/* level 3 */
		.on = {54, 0, 51, -1, -1, -1},
		.off = {52, 99, 47, -1, -1, -1},
		.rpm = {3800},
	},
	{
		/* level 4 */
		.on = {56, 50, 53, -1, -1, -1},
		.off = {53, 47, 49, -1, -1, -1},
		.rpm = {4100},
	},
	{
		/* level 5 */
		.on = {57, 52, 55, -1, -1, -1},
		.off = {55, 49, 51, -1, -1, -1},
		.rpm = {4400},
	},
	{
		/* level 6 */
		.on = {100, 100, 100, -1, -1, -1},
		.off = {56, 51, 53, -1, -1, -1},
		.rpm = {4900},
	},
};

#define NUM_FAN_LEVELS ARRAY_SIZE(fan_step_table)

BUILD_ASSERT(ARRAY_SIZE(fan_step_table) ==
	ARRAY_SIZE(fan_step_table));

int fan_table_to_rpm(int fan, int *temp)
{
	static int current_level;
	static int prev_tmp[TEMP_SENSOR_COUNT];
	int new_rpm = 0;
	int i;

	/*
	 * Compare the current and previous temperature, we have
	 * the three paths :
	 *  1. decreasing path. (check the release point)
	 *  2. increasing path. (check the trigger point)
	 *  3. invariant path. (return the current RPM)
	 */
	if (temp[TEMP_SENSOR_CHARGER] < prev_tmp[TEMP_SENSOR_CHARGER] ||
		temp[TEMP_SENSOR_MEMORY] < prev_tmp[TEMP_SENSOR_MEMORY] ||
		temp[TEMP_SENSOR_SOC] < prev_tmp[TEMP_SENSOR_SOC]) {
		for (i = current_level; i > 0; i--) {
			if (temp[TEMP_SENSOR_CHARGER] <
				fan_step_table[i].off[TEMP_SENSOR_CHARGER] &&
				temp[TEMP_SENSOR_MEMORY] <
				fan_step_table[i].off[TEMP_SENSOR_MEMORY] &&
				temp[TEMP_SENSOR_SOC] <
				fan_step_table[i].off[TEMP_SENSOR_SOC]) {
				current_level = i - 1;
			} else
				break;
		}
	} else if (temp[TEMP_SENSOR_CHARGER] > prev_tmp[TEMP_SENSOR_CHARGER] ||
			temp[TEMP_SENSOR_MEMORY]
				> prev_tmp[TEMP_SENSOR_MEMORY] ||
			temp[TEMP_SENSOR_SOC] > prev_tmp[TEMP_SENSOR_SOC]) {
		for (i = current_level; i < NUM_FAN_LEVELS; i++) {
			if ((temp[TEMP_SENSOR_CHARGER] >
				fan_step_table[i].on[TEMP_SENSOR_CHARGER] &&
				temp[TEMP_SENSOR_MEMORY] >
				fan_step_table[i].on[TEMP_SENSOR_MEMORY]) ||
				temp[TEMP_SENSOR_SOC] >
				fan_step_table[i].on[TEMP_SENSOR_SOC]) {
				current_level = i + 1;
			} else
				break;
		}
	}

	if (current_level < 0)
		current_level = 0;

	if (current_level >= NUM_FAN_LEVELS)
		current_level = NUM_FAN_LEVELS - 1;

	for (i = 0; i < TEMP_SENSOR_COUNT; ++i)
		prev_tmp[i] = temp[i];

	new_rpm = fan_step_table[current_level].rpm[FAN_CH_0];

	return new_rpm;
}

void board_override_fan_control(int fan, int *tmp)
{
	if (chipset_in_state(CHIPSET_STATE_ON |
		CHIPSET_STATE_ANY_SUSPEND)) {
		fan_set_rpm_mode(FAN_CH(fan), 1);
		fan_set_rpm_target(FAN_CH(fan),
			fan_table_to_rpm(fan, tmp));
	}
}


struct chg_curr_step {
	int on;
	int off;
	int curr_ma;
};

static const struct chg_curr_step chg_curr_table[] = {
	{.on =  0, .off =  0, .curr_ma = 3566},
	{.on = 65, .off = 64, .curr_ma = 2500},
	{.on = 69, .off = 68, .curr_ma = 1500},
};


#define NUM_CHG_CURRENT_LEVELS ARRAY_SIZE(chg_curr_table)

int charger_profile_override(struct charge_state_data *curr)
{
	int rv;
	int chg_temp_c;
	int current;
	int thermal_sensor_chrg;
	static int current_level;
	static int prev_tmp;


	if (!(curr->batt.flags & BATT_FLAG_RESPONSIVE))
		return 0;

	current = curr->requested_current;

	rv = temp_sensor_read(TEMP_SENSOR_CHARGER, &thermal_sensor_chrg);
	chg_temp_c = K_TO_C(thermal_sensor_chrg);

	if (rv != EC_SUCCESS)
		return rv;

	if (chipset_in_state(CHIPSET_STATE_ON)) {
		if (chg_temp_c < prev_tmp) {
			if ((chg_temp_c <= chg_curr_table[current_level].off)
				&& (current_level > 0))
				current_level -= 1;
		} else if (chg_temp_c > prev_tmp) {
			if ((chg_temp_c >= chg_curr_table[current_level + 1].on)
				&& (current_level < NUM_CHG_CURRENT_LEVELS - 1))
				current_level += 1;
		}

		prev_tmp = chg_temp_c;
		current = chg_curr_table[current_level].curr_ma;

		curr->requested_current = MIN(curr->requested_current, current);
	}

	return 0;
}

enum ec_status charger_profile_override_get_param(uint32_t param,
						  uint32_t *value)
{
	return EC_RES_INVALID_PARAM;
}

enum ec_status charger_profile_override_set_param(uint32_t param,
						  uint32_t value)
{
	return EC_RES_INVALID_PARAM;
}
