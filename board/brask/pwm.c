/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"

#include "compile_time_macros.h"
#include "hooks.h"
#include "pwm.h"
#include "pwm_chip.h"

const struct pwm_t pwm_channels[] = {
	[PWM_CH_FAN] = {
		.channel = 5,
		.flags = PWM_CONFIG_OPEN_DRAIN | PWM_CONFIG_DSLEEP,
		.freq = 1000
	},
};
BUILD_ASSERT(ARRAY_SIZE(pwm_channels) == PWM_CH_COUNT);

static void board_pwm_init(void)
{
	/*
	 * TODO(b/197478860): Turn on the fan at 100% by default
	 * We need to find tune the fan speed according to the
	 * thermal sensor value.
	 */
	pwm_enable(PWM_CH_FAN, 1);
	pwm_set_duty(PWM_CH_FAN, 100);
}
DECLARE_HOOK(HOOK_INIT, board_pwm_init, HOOK_PRIO_DEFAULT);
