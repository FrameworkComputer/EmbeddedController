/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Physical fans. These are logically separate from pwm_channels. */

#include "common.h"
#include "compile_time_macros.h"
#include "console.h"
#include "fan_chip.h"
#include "fan.h"
#include "hooks.h"
#include "pwm.h"

/* MFT channels. These are logically separate from pwm_channels. */
const struct mft_t mft_channels[] = {
	[MFT_CH_0] = {
		.module = NPCX_MFT_MODULE_1,
		.clk_src = TCKC_LFCLK,
		.pwm_id = PWM_CH_FAN,
	},
	[MFT_CH_1] = {
		.module = NPCX_MFT_MODULE_2,
		.clk_src = TCKC_LFCLK,
		.pwm_id = PWM_CH_FAN2,
	},
};
BUILD_ASSERT(ARRAY_SIZE(mft_channels) == MFT_CH_COUNT);

static const struct fan_conf fan_conf_0 = {
	.flags = FAN_USE_RPM_MODE,
	.ch = MFT_CH_0,	/* Use MFT id to control fan */
	.pgood_gpio = -1,
	.enable_gpio = GPIO_EN_PP5000_FAN,
};

static const struct fan_conf fan_conf_1 = {
	.flags = FAN_USE_RPM_MODE,
	.ch = MFT_CH_1,	/* Use MFT id to control fan */
	.pgood_gpio = -1,
	.enable_gpio = GPIO_EN_PP5000_FAN2,
};

static const struct fan_rpm fan_rpm_0 = {
	.rpm_min = 1900,
	.rpm_start = 2900,
	.rpm_max = 7300,
};

static const struct fan_rpm fan_rpm_1 = {
	.rpm_min = 1900,
	.rpm_start = 2900,
	.rpm_max = 7300,
};

const struct fan_t fans[FAN_CH_COUNT] = {
	[FAN_CH_0] = {
		.conf = &fan_conf_0,
		.rpm = &fan_rpm_0,
	},
	[FAN_CH_1] = {
		.conf = &fan_conf_1,
		.rpm = &fan_rpm_1,
	},
};

