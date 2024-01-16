/* Copyright 2023 The ChromiumOS Authors
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
#include "temp_sensor.h"

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
	.enable_gpio = GPIO_EN_PP5000_FAN,
};

/*
 * TOOD(b/181271666): thermistor placement and calibration
 *
 * Prototype fan spins at about 4200 RPM at 100% PWM, this
 * is specific to board ID 2 and might also apears in later
 * boards as well.
 */
static const struct fan_rpm fan_rpm_0 = {
	.rpm_min = 2450,
	.rpm_start = 2450,
	.rpm_max = 5500,
};

const struct fan_t fans[FAN_CH_COUNT] = {
	[FAN_CH_0] = {
		.conf = &fan_conf_0,
		.rpm = &fan_rpm_0,
	},
};

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

static const struct fan_step fan_table[] = {
	{
		/* level 0 */
		.on = { -1, 25, -1 },
		.off = { -1, 0, -1 },
		.rpm = { 0 },
	},
	{
		/* level 1 */
		.on = { -1, 38, -1 },
		.off = { -1, 34, -1 },
		.rpm = { 2450 },
	},
	{
		/* level 2 */
		.on = { -1, 41, -1 },
		.off = { -1, 37, -1 },
		.rpm = { 2600 },
	},
	{
		/* level 3 */
		.on = { -1, 44, -1 },
		.off = { -1, 40, -1 },
		.rpm = { 2800 },
	},
	{
		/* level 4 */
		.on = { -1, 47, -1 },
		.off = { -1, 43, -1 },
		.rpm = { 3100 },
	},
	{
		/* level 5 */
		.on = { -1, 52, -1 },
		.off = { -1, 48, -1 },
		.rpm = { 3300 },
	},
	{
		/* level 4 */
		.on = { -1, 47, -1 },
		.off = { -1, 43, -1 },
		.rpm = { 3100 },
	},
	{
		/* level 5 */
		.on = { -1, 52, -1 },
		.off = { -1, 48, -1 },
		.rpm = { 3300 },
	},
	{
		/* level 6 */
		.on = { -1, 60, -1 },
		.off = { -1, 56, -1 },
		.rpm = { 3700 },
	},
	{
		/* level 7 */
		.on = { -1, 63, -1 },
		.off = { -1, 59, -1 },
		.rpm = { 4000 },
	},
	{
		/* level 8 */
		.on = { -1, 66, -1 },
		.off = { -1, 62, -1 },
		.rpm = { 4300 },
	},
	{
		/* level 9 */
		.on = { -1, 69, -1 },
		.off = { -1, 65, -1 },
		.rpm = { 4600 },
	},
	{
		/* level 10 */
		.on = { -1, 75, -1 },
		.off = { -1, 72, -1 },
		.rpm = { 5500 },
	},
};

const int num_fan_levels = ARRAY_SIZE(fan_table);

#define NUM_FAN_LEVELS ARRAY_SIZE(fan_table)

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
			if (temp[temp_sensor] <=
			    fan_step_table[i].off[temp_sensor])
				current_level = i - 1;
			else
				break;
		}
	} else if (temp[temp_sensor] > prev_tmp[temp_sensor]) {
		for (i = current_level; i < fan_table_size; i++) {
			if (temp[temp_sensor] >=
			    fan_step_table[i].on[temp_sensor])
				current_level = i;
			else
				break;
		}
	}

	if (current_level < 0)
		current_level = 0;

	if (current_level >= fan_table_size)
		current_level = fan_table_size - 1;

	prev_tmp[temp_sensor] = temp[temp_sensor];

	return fan_step_table[current_level].rpm[fan];
}

void board_override_fan_control(int fan, int *tmp)
{
	if (chipset_in_state(CHIPSET_STATE_ON | CHIPSET_STATE_ANY_SUSPEND)) {
		int new_rpm = fan_table_to_rpm(fan, tmp, TEMP_SENSOR_2_AMBIENT);

		if (new_rpm != fan_get_rpm_target(FAN_CH(fan))) {
			ccprints("Setting fan RPM to %d", new_rpm);
			fan_set_rpm_mode(FAN_CH(fan), 1);
			fan_set_rpm_target(FAN_CH(fan), new_rpm);
		}
	}
}

#ifndef CONFIG_FANS

/*
 * TODO(b/181271666): use static fan speeds until fan and sensors are
 * tuned. for now, use:
 *
 *   AP off:  33%
 *   AP  on: 100%
 */

static void fan_slow(void)
{
	const int duty_pct = 33;

	ccprints("%s: speed %d%%", __func__, duty_pct);

	pwm_enable(PWM_CH_FAN, 1);
	pwm_set_duty(PWM_CH_FAN, duty_pct);
}

static void fan_max(void)
{
	const int duty_pct = 100;

	ccprints("%s: speed %d%%", __func__, duty_pct);

	pwm_enable(PWM_CH_FAN, 1);
	pwm_set_duty(PWM_CH_FAN, duty_pct);
}

DECLARE_HOOK(HOOK_INIT, fan_slow, HOOK_PRIO_DEFAULT);
DECLARE_HOOK(HOOK_CHIPSET_SUSPEND, fan_slow, HOOK_PRIO_DEFAULT);
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, fan_slow, HOOK_PRIO_DEFAULT);
DECLARE_HOOK(HOOK_CHIPSET_RESET, fan_max, HOOK_PRIO_FIRST);
DECLARE_HOOK(HOOK_CHIPSET_RESUME, fan_max, HOOK_PRIO_DEFAULT);

#endif /* CONFIG_FANS */
