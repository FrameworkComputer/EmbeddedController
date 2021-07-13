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
	[PWM_CH_LED2] = {
		.channel = 0,
		.flags = PWM_CONFIG_ACTIVE_LOW | PWM_CONFIG_DSLEEP,
		.freq = 4800,
	},
	[PWM_CH_LED3] = {
		.channel = 1,
		.flags = PWM_CONFIG_ACTIVE_LOW | PWM_CONFIG_DSLEEP,
		.freq = 4800,
	},
	[PWM_CH_LED1] = {
		.channel = 2,
		.flags = PWM_CONFIG_ACTIVE_LOW | PWM_CONFIG_DSLEEP,
		.freq = 4800,
	},
	[PWM_CH_KBLIGHT] = {
		.channel = 3,
		.flags = 0,
		/*
		 * Set PWM frequency to multiple of 50 Hz and 60 Hz to prevent
		 * flicker. Higher frequencies consume similar average power to
		 * lower PWM frequencies, but higher frequencies record a much
		 * lower maximum power.
		 */
		.freq = 2400,
	},
	[PWM_CH_FAN] = {
		.channel = 5,
		.flags = PWM_CONFIG_OPEN_DRAIN,
		.freq = 25000
	},
	[PWM_CH_LED4] = {
		.channel = 7,
		.flags = PWM_CONFIG_ACTIVE_LOW | PWM_CONFIG_DSLEEP,
		.freq = 4800,
	},
};
BUILD_ASSERT(ARRAY_SIZE(pwm_channels) == PWM_CH_COUNT);

static void board_pwm_init(void)
{
	/*
	 * Turn off LED1 to LED4.
	 * Turn on KB LED at 50%.
	 */
	pwm_enable(PWM_CH_LED1, 1);
	pwm_set_duty(PWM_CH_LED1, 0);
	pwm_enable(PWM_CH_LED2, 1);
	pwm_set_duty(PWM_CH_LED2, 0);
	pwm_enable(PWM_CH_LED3, 1);
	pwm_set_duty(PWM_CH_LED3, 0);
	pwm_enable(PWM_CH_LED4, 1);
	pwm_set_duty(PWM_CH_LED4, 0);

	pwm_enable(PWM_CH_KBLIGHT, 1);
	pwm_set_duty(PWM_CH_KBLIGHT, 50);
}
DECLARE_HOOK(HOOK_INIT, board_pwm_init, HOOK_PRIO_DEFAULT);
