/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "chipset.h"
#include "common.h"
#include "console.h"
#include "extpower.h"
#include "fan.h"
#include "hooks.h"
#include "host_command.h"
#include "lid_switch.h"
#include "motion_lid.h"
#include "tablet_mode.h"
#include "temp_sensor.h"
#include "thermal.h"
#include "util.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_THERMAL, outstr)
#define CPRINTS(format, args...) cprints(CC_THERMAL, format, ## args)

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

static const struct fan_step *fan_step_table;

static const struct fan_step fan1_table_clamshell[] = {
	{
		/* level 0 */
		.on = {-1, -1, -1, -1},
		.off = {-1, -1, -1, -1},
		.rpm = {0},
	},
	{
		/* level 1 */
		.on = {-1, -1, 40, -1},
		.off = {-1, -1, 31, -1},
		.rpm = {1900},
	},
	{
		/* level 2 */
		.on = {-1, -1, 45, -1},
		.off = {-1, -1, 43, -1},
		.rpm = {2900},
	},
	{
		/* level 3 */
		.on = {-1, -1, 48, -1},
		.off = {-1, -1, 46, -1},
		.rpm = {3200},
	},
	{
		/* level 4 */
		.on = {-1, -1, 51, -1},
		.off = {-1, -1, 49, -1},
		.rpm = {3550},
	},
	{
		/* level 5 */
		.on = {-1, -1, 54, -1},
		.off = {-1, -1, 52, -1},
		.rpm = {3950},
	},
	{
		/* level 6 */
		.on = {-1, -1, 57, -1},
		.off = {-1, -1, 55, -1},
		.rpm = {4250},
	},
	{
		/* level 7 */
		.on = {-1, -1, 60, -1},
		.off = {-1, -1, 58, -1},
		.rpm = {4650},
	},
};

static const struct fan_step fan1_table_tablet[] = {
	{
		/* level 0 */
		.on = {-1, -1, -1, -1},
		.off = {-1, -1, -1, -1},
		.rpm = {0},
	},
	{
		/* level 1 */
		.on = {-1, -1, 41, -1},
		.off = {-1, -1, 31, -1},
		.rpm = {2100},
	},
	{
		/* level 2 */
		.on = {-1, -1, 50, -1},
		.off = {-1, -1, 48, -1},
		.rpm = {2600},
	},
	{
		/* level 3 */
		.on = {-1, -1, 54, -1},
		.off = {-1, -1, 52, -1},
		.rpm = {2800},
	},
	{
		/* level 4 */
		.on = {-1, -1, 57, -1},
		.off = {-1, -1, 55, -1},
		.rpm = {3300},
	},
	{
		/* level 5 */
		.on = {-1, -1, 60, -1},
		.off = {-1, -1, 58, -1},
		.rpm = {3800},
	},
	{
		/* level 6 */
		.on = {-1, -1, 72, -1},
		.off = {-1, -1, 69, -1},
		.rpm = {4000},
	},
	{
		/* level 7 */
		.on = {-1, -1, 74, -1},
		.off = {-1, -1, 73, -1},
		.rpm = {4300},
	},
};

static const struct fan_step fan1_table_stand[] = {
	{
		/* level 0 */
		.on = {-1, -1, -1, -1},
		.off = {-1, -1, -1, -1},
		.rpm = {0},
	},
	{
		/* level 1 */
		.on = {-1, -1, 34, -1},
		.off = {-1, -1, 31, -1},
		.rpm = {1850},
	},
	{
		/* level 2 */
		.on = {-1, -1, 42, -1},
		.off = {-1, -1, 39, -1},
		.rpm = {2550},
	},
	{
		/* level 3 */
		.on = {-1, -1, 49, -1},
		.off = {-1, -1, 48, -1},
		.rpm = {2900},
	},
	{
		/* level 4 */
		.on = {-1, -1, 51, -1},
		.off = {-1, -1, 50, -1},
		.rpm = {3350},
	},
	{
		/* level 5 */
		.on = {-1, -1, 53, -1},
		.off = {-1, -1, 52, -1},
		.rpm = {3700},
	},
	{
		/* level 6 */
		.on = {-1, -1, 55, -1},
		.off = {-1, -1, 54, -1},
		.rpm = {3900},
	},
	{
		/* level 7 */
		.on = {-1, -1, 57, -1},
		.off = {-1, -1, 56, -1},
		.rpm = {4250},
	},
};

static const struct fan_step fan0_table_clamshell[] = {
	{
		/* level 0 */
		.on = {-1, -1, -1, -1},
		.off = {-1, -1, -1, -1},
		.rpm = {0},
	},
	{
		/* level 1 */
		.on = {-1, -1, 41, -1},
		.off = {-1, -1, 31, -1},
		.rpm = {2350},
	},
	{
		/* level 2 */
		.on = {-1, -1, 44, -1},
		.off = {-1, -1, 42, -1},
		.rpm = {3300},
	},
	{
		/* level 3 */
		.on = {-1, -1, 47, -1},
		.off = {-1, -1, 45, -1},
		.rpm = {3600},
	},
	{
		/* level 4 */
		.on = {-1, -1, 50, -1},
		.off = {-1, -1, 48, -1},
		.rpm = {4050},
	},
	{
		/* level 5 */
		.on = {-1, -1, 53, -1},
		.off = {-1, -1, 51, -1},
		.rpm = {4450},
	},
	{
		/* level 6 */
		.on = {-1, -1, 56, -1},
		.off = {-1, -1, 54, -1},
		.rpm = {4750},
	},
	{
		/* level 7 */
		.on = {-1, -1, 59, -1},
		.off = {-1, -1, 57, -1},
		.rpm = {5150},
	},
};

static const struct fan_step fan0_table_tablet[] = {
	{
		/* level 0 */
		.on = {-1, -1, -1, -1},
		.off = {-1, -1, -1, -1},
		.rpm = {0},
	},
	{
		/* level 1 */
		.on = {-1, -1, 41, -1},
		.off = {-1, -1, 31, -1},
		.rpm = {2250},
	},
	{
		/* level 2 */
		.on = {-1, -1, 50, -1},
		.off = {-1, -1, 48, -1},
		.rpm = {2850},
	},
	{
		/* level 3 */
		.on = {-1, -1, 54, -1},
		.off = {-1, -1, 51, -1},
		.rpm = {3100},
	},
	{
		/* level 4 */
		.on = {-1, -1, 57, -1},
		.off = {-1, -1, 55, -1},
		.rpm = {3500},
	},
	{
		/* level 5 */
		.on = {-1, -1, 60, -1},
		.off = {-1, -1, 58, -1},
		.rpm = {3900},
	},
	{
		/* level 6 */
		.on = {-1, -1, 72, -1},
		.off = {-1, -1, 69, -1},
		.rpm = {4150},
	},
	{
		/* level 7 */
		.on = {-1, -1, 74, -1},
		.off = {-1, -1, 73, -1},
		.rpm = {4400},
	},
};

static const struct fan_step fan0_table_stand[] = {
	{
		/* level 0 */
		.on = {-1, -1, -1, -1},
		.off = {-1, -1, -1, -1},
		.rpm = {0},
	},
	{
		/* level 1 */
		.on = {-1, -1, 34, -1},
		.off = {-1, -1, 31, -1},
		.rpm = {2250},
	},
	{
		/* level 2 */
		.on = {-1, -1, 42, -1},
		.off = {-1, -1, 39, -1},
		.rpm = {2800},
	},
	{
		/* level 3 */
		.on = {-1, -1, 49, -1},
		.off = {-1, -1, 48, -1},
		.rpm = {3150},
	},
	{
		/* level 4 */
		.on = {-1, -1, 51, -1},
		.off = {-1, -1, 50, -1},
		.rpm = {3550},
	},
	{
		/* level 5 */
		.on = {-1, -1, 53, -1},
		.off = {-1, -1, 52, -1},
		.rpm = {3900},
	},
	{
		/* level 6 */
		.on = {-1, -1, 55, -1},
		.off = {-1, -1, 54, -1},
		.rpm = {4150},
	},
	{
		/* level 7 */
		.on = {-1, -1, 57, -1},
		.off = {-1, -1, 56, -1},
		.rpm = {4400},
	},
};

#define NUM_FAN_LEVELS ARRAY_SIZE(fan1_table_clamshell)

#define lid_angle_tablet	340
static int throttle_on;

BUILD_ASSERT(ARRAY_SIZE(fan1_table_clamshell) ==
	ARRAY_SIZE(fan1_table_tablet));

#define average_time 60
int fan_table_to_rpm(int fan, int *temp)
{
	static int current_level;
	static int avg_tmp[TEMP_SENSOR_COUNT];
	static int avg_calc_tmp[TEMP_SENSOR_COUNT][average_time];
	static int prev_tmp[TEMP_SENSOR_COUNT];
	static int new_rpm;
	int i, j, avg_sum = 0;
	int lid_angle = motion_lid_get_angle();
	static int fan_up_count, fan_down_count;
	static int temp_count;

	/*
	 * Select different fan curve table
	 * by mode: clamshell, tent/stand, tablet and fan id
	 */
	if (tablet_get_mode()) {
		if (gpio_get_level(GPIO_FAN_ID))
			fan_step_table = fan1_table_stand;
		else
			fan_step_table = fan0_table_stand;

		if (lid_angle >= lid_angle_tablet) {
			if (gpio_get_level(GPIO_FAN_ID))
				fan_step_table = fan1_table_tablet;
			else
				fan_step_table = fan0_table_tablet;
		}
	} else {
		if (gpio_get_level(GPIO_FAN_ID))
			fan_step_table = fan1_table_clamshell;
		else
			fan_step_table = fan0_table_clamshell;
	}

	/*
	 * Average temp 60 sec timing average
	 */
	if (temp_count < average_time) {
		avg_calc_tmp[TEMP_SENSOR_CPU][temp_count] =
			temp[TEMP_SENSOR_CPU];
		temp_count++;
	} else
		temp_count = 0;

	for (j = 0; j < average_time; j++)
		avg_sum = avg_sum + avg_calc_tmp[TEMP_SENSOR_CPU][j];

	avg_tmp[TEMP_SENSOR_CPU] = avg_sum/average_time;

	/*
	 * Compare the current and previous temperature, we have
	 * the three paths :
	 *  1. decreasing path. (check the release point)
	 *  2. increasing path. (check the trigger point)
	 *  3. invariant path. (return the current RPM)
	 */
	if (avg_tmp[TEMP_SENSOR_CPU] < prev_tmp[TEMP_SENSOR_CPU]) {
		for (i = current_level; i >= 0; i--) {
			if (avg_tmp[TEMP_SENSOR_CPU] <
				fan_step_table[i].off[TEMP_SENSOR_CPU]) {
			/*
			 * fan step down debounce
			 */
				if (fan_down_count < 10) {
					fan_down_count++;
					fan_up_count = 0;

					return new_rpm;
				}
				fan_down_count = 0;
				fan_up_count = 0;

				current_level = i - 1;
			} else
				break;
		}
	} else if (avg_tmp[TEMP_SENSOR_CPU] > prev_tmp[TEMP_SENSOR_CPU]) {
		for (i = current_level+1; i < NUM_FAN_LEVELS; i++) {
			if ((avg_tmp[TEMP_SENSOR_CPU] >
				 fan_step_table[i].on[TEMP_SENSOR_CPU])) {
			/*
			 * fan step up debounce
			 */
				if (fan_up_count < 10) {
					fan_up_count++;
					fan_down_count = 0;

					return new_rpm;
				}
				fan_down_count = 0;
				fan_up_count = 0;

				current_level = i;
			} else
				break;
		}
	} else {
		fan_down_count = 0;
		fan_up_count = 0;
	}

	if (current_level < 1)
		current_level = 1;

	if (current_level >= 7)
		current_level = 7;

	for (i = 0; i < TEMP_SENSOR_COUNT; ++i)
		prev_tmp[i] = avg_tmp[i];

	ASSERT(current_level < NUM_FAN_LEVELS);

	switch (fan) {
	case FAN_CH_0:
		new_rpm = fan_step_table[current_level].rpm[FAN_CH_0];
		break;
	default:
		break;
	}

	return new_rpm;
}

void board_override_fan_control(int fan, int *tmp)
{
	if (chipset_in_state(CHIPSET_STATE_ON | CHIPSET_STATE_ANY_SUSPEND)) {
		int new_rpm = fan_table_to_rpm(fan, tmp);

		if (new_rpm != fan_get_rpm_target(FAN_CH(fan))) {
			cprints(CC_THERMAL, "Setting fan RPM to %d", new_rpm);
			board_print_temps();
			fan_set_rpm_mode(FAN_CH(fan), 1);
			fan_set_rpm_target(FAN_CH(fan), new_rpm);
		}
	}
}

void thermal_protect(void)
{
	int thermal_sensor1, thermal_sensor2;

	temp_sensor_read(TEMP_SENSOR_5V_REGULATOR, &thermal_sensor1);
	temp_sensor_read(TEMP_SENSOR_CPU, &thermal_sensor2);

	if ((!lid_is_open()) && (!extpower_is_present())) {
		if (thermal_sensor2 > C_TO_K(70)) {
			chipset_throttle_cpu(1);
			throttle_on = 1;
		} else if (thermal_sensor2 < C_TO_K(60) && throttle_on) {
			chipset_throttle_cpu(0);
			throttle_on = 0;
		}

		if (thermal_sensor1 > C_TO_K(51))
			chipset_force_shutdown(CHIPSET_SHUTDOWN_THERMAL);
	}
}
DECLARE_HOOK(HOOK_SECOND, thermal_protect, HOOK_PRIO_DEFAULT);
