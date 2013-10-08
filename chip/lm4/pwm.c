/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* PWM control module for LM4 */

#include "clock.h"
#include "gpio.h"
#include "hooks.h"
#include "pwm.h"
#include "pwm_data.h"
#include "registers.h"
#include "util.h"

/* Maximum RPM for PWM controller */
#define MAX_RPM 0x1fff

/* Maximum PWM for PWM controller */
#define MAX_PWM 0x1ff

#define RPM_SCALE 2

void pwm_enable(enum pwm_channel ch, int enabled)
{
	const struct pwm_t *pwm = pwm_channels + ch;
	if (enabled)
		LM4_FAN_FANCTL |= (1 << pwm->channel);
	else
		LM4_FAN_FANCTL &= ~(1 << pwm->channel);
}

int pwm_get_enabled(enum pwm_channel ch)
{
	const struct pwm_t *pwm = pwm_channels + ch;
	return (LM4_FAN_FANCTL & (1 << pwm->channel)) ? 1 : 0;
}

void pwm_set_duty(enum pwm_channel ch, int percent)
{
	const struct pwm_t *pwm = pwm_channels + ch;
	int duty;

	if (percent < 0)
		percent = 0;
	else if (percent > 100)
		percent = 100;

	if (pwm->flags & PWM_CONFIG_ACTIVE_LOW)
		percent = 100 - percent;

	duty = (MAX_PWM * percent + 50) / 100;

	/* Always enable the channel */
	pwm_enable(ch, 1);

	/* Set the duty cycle */
	LM4_FAN_FANCMD(pwm->channel) = duty << 16;
}

int pwm_get_duty(enum pwm_channel ch)
{
	const struct pwm_t *pwm = pwm_channels + ch;
	int percent = ((LM4_FAN_FANCMD(pwm->channel) >> 16) * 100 + MAX_PWM / 2)
		/ MAX_PWM;

	if (pwm->flags & PWM_CONFIG_ACTIVE_LOW)
		percent = 100 - percent;

	return percent;
}

static void pwm_init(void)
{
	int i;
	const struct pwm_t *pwm;

	/* Enable the fan module and delay a few clocks */
	clock_enable_peripheral(CGC_OFFSET_FAN, 0x1,
			CGC_MODE_RUN | CGC_MODE_SLEEP);

	/* Disable all fans */
	LM4_FAN_FANCTL = 0;

	for (i = 0; i < PWM_CH_COUNT; ++i) {
		pwm = pwm_channels + i;

		if (pwm->flags & PWM_CONFIG_HAS_RPM_MODE) {
			/*
			 * Configure PWM:
			 * 0x8000 = bit 15     = auto-restart
			 * 0x0000 = bit 14     = slow acceleration
			 * 0x0000 = bits 13:11 = no hysteresis
			 * 0x0000 = bits 10:8  = start period (2<<0) edges
			 * 0x0000 = bits 7:6   = no fast start
			 * 0x0020 = bits 5:4   = average 4 edges when
			 *                       calculating RPM
			 * 0x000c = bits 3:2   = 8 pulses per revolution
			 *                       (see note at top of file)
			 * 0x0000 = bit 0      = automatic control
			 */
			LM4_FAN_FANCH(pwm->channel) = 0x802c;
		} else {
			/*
			 * Configure keyboard backlight:
			 * 0x0000 = bit 15     = no auto-restart
			 * 0x0000 = bit 14     = slow acceleration
			 * 0x0000 = bits 13:11 = no hysteresis
			 * 0x0000 = bits 10:8  = start period (2<<0) edges
			 * 0x0000 = bits 7:6   = no fast start
			 * 0x0000 = bits 5:4   = no RPM averaging
			 * 0x0000 = bits 3:2   = 1 pulses per revolution
			 * 0x0001 = bit 0      = manual control
			 */
			LM4_FAN_FANCH(pwm->channel) = 0x0001;
		}
	}
}
DECLARE_HOOK(HOOK_INIT, pwm_init, HOOK_PRIO_INIT_PWM);
