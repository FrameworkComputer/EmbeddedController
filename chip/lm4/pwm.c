/* Copyright 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* PWM control module for LM4.
 *
 * On this chip, the PWM logic is implemented by the hardware FAN modules.
 */

#include "clock.h"
#include "fan.h"
#include "gpio.h"
#include "hooks.h"
#include "pwm.h"
#include "pwm_chip.h"
#include "registers.h"
#include "util.h"

void pwm_enable(enum pwm_channel ch, int enabled)
{
	fan_set_enabled(pwm_channels[ch].channel, enabled);
}

int pwm_get_enabled(enum pwm_channel ch)
{
	return fan_get_enabled(pwm_channels[ch].channel);
}

void pwm_set_duty(enum pwm_channel ch, int percent)
{
	if (percent < 0)
		percent = 0;
	else if (percent > 100)
		percent = 100;

	/* Assume the fan control is active high and invert it ourselves */
	if (pwm_channels[ch].flags & PWM_CONFIG_ACTIVE_LOW)
		percent = 100 - percent;

	/* Always enable the channel */
	pwm_enable(ch, 1);

	/* Set the duty cycle */
	fan_set_duty(pwm_channels[ch].channel, percent);
}

int pwm_get_duty(enum pwm_channel ch)
{
	int percent = fan_get_duty(pwm_channels[ch].channel);

	if (pwm_channels[ch].flags & PWM_CONFIG_ACTIVE_LOW)
		percent = 100 - percent;

	return percent;
}

static void pwm_init(void)
{
	int i;

	for (i = 0; i < PWM_CH_COUNT; ++i)
		fan_channel_setup(pwm_channels[i].channel,
				       (pwm_channels[i].flags &
					PWM_CONFIG_HAS_RPM_MODE)
				       ? FAN_USE_RPM_MODE : 0);
}

/* The chip-specific fan module initializes before this. */
DECLARE_HOOK(HOOK_INIT, pwm_init, HOOK_PRIO_INIT_PWM);
