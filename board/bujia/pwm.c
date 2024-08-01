/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "compile_time_macros.h"
#include "hooks.h"
#include "pwm.h"
#include "pwm_chip.h"

const struct pwm_t pwm_channels[] = {
	[PWM_CH_LED_GREEN] = { .channel = 0,
			       .flags = PWM_CONFIG_ACTIVE_LOW |
					PWM_CONFIG_DSLEEP,
			       .freq = 2000 },
	[PWM_CH_FAN] = { .channel = 5,
			 .flags = PWM_CONFIG_OPEN_DRAIN,
			 .freq = 25000 },
	[PWM_CH_LED_RED] = { .channel = 2,
			     .flags = PWM_CONFIG_ACTIVE_LOW | PWM_CONFIG_DSLEEP,
			     .freq = 2000 },
};
BUILD_ASSERT(ARRAY_SIZE(pwm_channels) == PWM_CH_COUNT);

static void board_pwm_init(void)
{
	pwm_enable(PWM_CH_FAN, 1);

	pwm_enable(PWM_CH_LED_RED, 1);
	pwm_enable(PWM_CH_LED_GREEN, 1);
}
DECLARE_HOOK(HOOK_INIT, board_pwm_init, HOOK_PRIO_DEFAULT);
