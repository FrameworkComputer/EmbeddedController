/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Physical fans. These are logically separate from pwm_channels. */

#include "common.h"
#include "compile_time_macros.h"
#include "console.h"
#include "ec_commands.h"
#include "fan.h"
#include "fan_chip.h"
#include "hooks.h"
#include "pwm.h"

/* MFT channels. These are logically separate from pwm_channels. */
const struct mft_t mft_channels[] = {
	[MFT_CH_0] = {
		.module = NPCX_MFT_MODULE_2,
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

/*
 * TOOD(b/197478860): need to update for real fan
 *
 * Prototype fan spins at about 7200 RPM at 100% PWM.
 * Set minimum at around 30% PWM.
 */
static const struct fan_rpm fan_rpm_0 = {
	.rpm_min = 2400,
	.rpm_start = 2400,
	.rpm_max = 5300,
};

const struct fan_t fans[FAN_CH_COUNT] = {
	[FAN_CH_0] = {
		.conf = &fan_conf_0,
		.rpm = &fan_rpm_0,
	},
};

static const int temp_fan_off = C_TO_K(35);
static const int temp_fan_max = C_TO_K(55);

static const struct fan_step_1_1 fan_table0[] = {
	{ .decreasing_temp_ratio_threshold = TEMP_TO_RATIO(35),
	  .increasing_temp_ratio_threshold = TEMP_TO_RATIO(41),
	  .rpm = 2400 },
	{ .decreasing_temp_ratio_threshold = TEMP_TO_RATIO(40),
	  .increasing_temp_ratio_threshold = TEMP_TO_RATIO(44),
	  .rpm = 2900 },
	{ .decreasing_temp_ratio_threshold = TEMP_TO_RATIO(42),
	  .increasing_temp_ratio_threshold = TEMP_TO_RATIO(46),
	  .rpm = 3400 },
	{ .decreasing_temp_ratio_threshold = TEMP_TO_RATIO(44),
	  .increasing_temp_ratio_threshold = TEMP_TO_RATIO(48),
	  .rpm = 3900 },
	{ .decreasing_temp_ratio_threshold = TEMP_TO_RATIO(46),
	  .increasing_temp_ratio_threshold = TEMP_TO_RATIO(50),
	  .rpm = 4400 },
	{ .decreasing_temp_ratio_threshold = TEMP_TO_RATIO(48),
	  .increasing_temp_ratio_threshold = TEMP_TO_RATIO(52),
	  .rpm = 4900 },
	{ .decreasing_temp_ratio_threshold = TEMP_TO_RATIO(50),
	  .increasing_temp_ratio_threshold = TEMP_TO_RATIO(55),
	  .rpm = 5300 },
};
#define NUM_FAN_LEVELS ARRAY_SIZE(fan_table0)

static const struct fan_step_1_1 *fan_table = fan_table0;

int fan_percent_to_rpm(int fan, int temp_ratio)
{
	return temp_ratio_to_rpm_hysteresis(fan_table, NUM_FAN_LEVELS, fan,
					    temp_ratio, NULL);
}
