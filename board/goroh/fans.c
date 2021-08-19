/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Physical fans. These are logically separate from pwm_channels. */

#include "common.h"
#include "compile_time_macros.h"
#include "console.h"
#include "fan.h"
#include "hooks.h"
#include "pwm.h"
#include "pwm_chip.h"

const struct fan_conf fan_conf_0 = {
	.flags = FAN_USE_RPM_MODE,
	.ch = PWM_CH_FAN,
	.pgood_gpio = -1,
	.enable_gpio = GPIO_EN_PP5000_FAN_X,
};

const struct fan_rpm fan_rpm_0 = {
	.rpm_min = 2400,
	.rpm_start = 2400,
	.rpm_max = 5700,
};

const struct fan_t fans[] = {
	[FAN_CH_0] = {
		.conf = &fan_conf_0,
		.rpm = &fan_rpm_0,
	},
};
BUILD_ASSERT(ARRAY_SIZE(fans) == CONFIG_FANS);
/*
 * PWM HW channelx binding tachometer channelx for fan control.
 * Four tachometer input pins but two tachometer modules only,
 * so always binding [TACH_CH_TACH0A | TACH_CH_TACH0B] and/or
 * [TACH_CH_TACH1A | TACH_CH_TACH1B]
 */
const struct fan_tach_t fan_tach[] = {
	[PWM_HW_CH_DCR0] = {
		.ch_tach = TACH_CH_NULL,
		.fan_p = -1,
		.rpm_re = -1,
		.s_duty = -1,
	},
	[PWM_HW_CH_DCR1] = {
		.ch_tach = TACH_CH_NULL,
		.fan_p = -1,
		.rpm_re = -1,
		.s_duty = -1,
	},
	[PWM_HW_CH_DCR2] = {
		.ch_tach = TACH_CH_TACH0A,
		.fan_p = 2,
		.rpm_re = 50,
		.s_duty = 30,
	},
	[PWM_HW_CH_DCR3] = {
		.ch_tach = TACH_CH_NULL,
		.fan_p = -1,
		.rpm_re = -1,
		.s_duty = -1,
	},
	[PWM_HW_CH_DCR4] = {
		.ch_tach = TACH_CH_NULL,
		.fan_p = -1,
		.rpm_re = -1,
		.s_duty = -1,
	},
	[PWM_HW_CH_DCR5] = {
		.ch_tach = TACH_CH_NULL,
		.fan_p = -1,
		.rpm_re = -1,
		.s_duty = -1,
	},
	[PWM_HW_CH_DCR6] = {
		.ch_tach = TACH_CH_NULL,
		.fan_p = -1,
		.rpm_re = -1,
		.s_duty = -1,
	},
	[PWM_HW_CH_DCR7] = {
		.ch_tach = TACH_CH_NULL,
		.fan_p = -1,
		.rpm_re = -1,
		.s_duty = -1,
	},
};
