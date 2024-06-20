/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Physical fans. These are logically separate from pwm_channels. */

#include "chipset.h"
#include "common.h"
#include "compile_time_macros.h"
#include "console.h"
#include "fan.h"
#include "fan_chip.h"
#include "hooks.h"
#include "pwm.h"

/* MFT channels. These are logically separate from pwm_channels. */
const struct mft_t mft_channels[] = {
	[MFT_CH_0] = {
		.module = NPCX_MFT_MODULE_1,
		.clk_src = TCKC_LFCLK,
		.pwm_id = PWM_CH_FAN,
	},
};
BUILD_ASSERT(ARRAY_SIZE(mft_channels) == MFT_CH_COUNT);

static const struct fan_conf fan_conf_0 = {
	.flags = FAN_USE_RPM_MODE,
	.ch = MFT_CH_0, /* Use MFT id to control fan */
	.pgood_gpio = -1,
	.enable_gpio = -1,
};

static const struct fan_rpm fan_rpm_0 = {
	.rpm_min = 0,
	.rpm_start = 1230,
	.rpm_max = 4100,
};

const struct fan_t fans[FAN_CH_COUNT] = {
	[FAN_CH_0] = {
		.conf = &fan_conf_0,
		.rpm = &fan_rpm_0,
	},
};

struct fan_step {
	int on;
	int off;
	int rpm;
};

static const struct fan_step fan_table[] = {
	{ .on = 38, .off = 0, .rpm = 0 },
	{ .on = 41, .off = 34, .rpm = 2100 },
	{ .on = 44, .off = 37, .rpm = 2400 },
	{ .on = 47, .off = 40, .rpm = 2700 },
	{ .on = 50, .off = 43, .rpm = 3100 },
	{ .on = 52, .off = 46, .rpm = 3500 },
	{ .on = 127, .off = 49, .rpm = 4100 },
};

int fan_table_to_rpm(int fan, int *temp, enum temp_sensor_id temp_sensor)
{
	const struct fan_step *fan_step_table;
	/* current fan level */
	static int current_level;
	/* previous sensor temperature */
	static int prev_tmp[TEMP_SENSOR_COUNT];

	int i;
	uint8_t fan_table_size;

	fan_step_table = fan_table;
	fan_table_size = ARRAY_SIZE(fan_table);

	/*
	 * Compare the current and previous temperature, we have
	 * the three paths :
	 *  1. decreasing path. (check the release point)
	 *  2. increasing path. (check the trigger point)
	 *  3. invariant path. (return the current RPM)
	 */

	if (temp[temp_sensor] < prev_tmp[temp_sensor]) {
		for (i = current_level; i > 0; i--) {
			if (temp[temp_sensor] <= fan_step_table[i].off)
				current_level = i - 1;
			else
				break;
		}
	} else if (temp[temp_sensor] > prev_tmp[temp_sensor]) {
		for (i = current_level; i < fan_table_size; i++) {
			if (temp[temp_sensor] >= fan_step_table[i].on)
				current_level = i + 1;
			else
				break;
		}
	}

	if (current_level < 0)
		current_level = 0;

	if (current_level >= fan_table_size)
		current_level = fan_table_size - 1;

	prev_tmp[temp_sensor] = temp[temp_sensor];

	return fan_step_table[current_level].rpm;
}

void board_override_fan_control(int fan, int *tmp)
{
	if (chipset_in_state(CHIPSET_STATE_ON | CHIPSET_STATE_ANY_SUSPEND)) {
		int new_rpm = fan_table_to_rpm(fan, tmp, TEMP_SENSOR_2);

		if (new_rpm != fan_get_rpm_target(FAN_CH(fan))) {
			fan_set_rpm_mode(FAN_CH(fan), 1);
			fan_set_rpm_target(FAN_CH(fan), new_rpm);
		}
	}
}
