/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* PWM control module for MEC1322 */

#include "hooks.h"
#include "pwm.h"
#include "pwm_chip.h"
#include "registers.h"
#include "util.h"

/*
 * PWMs that must remain active in low-power idle - MEC1322_PCR_EC_SLP_EN
 * bit mask.
 */
static uint32_t pwm_keep_awake_mask;

void pwm_enable(enum pwm_channel ch, int enabled)
{
	int id = pwm_channels[ch].channel;

	if (enabled) {
		MEC1322_PWM_CFG(id) |= 0x1;
		if (pwm_channels[ch].flags & PWM_CONFIG_DSLEEP)
			pwm_keep_awake_mask |=
				MEC1322_PCR_EC_SLP_EN_PWM(id);
	} else {
		MEC1322_PWM_CFG(id) &= ~0x1;
		pwm_keep_awake_mask &= ~MEC1322_PCR_EC_SLP_EN_PWM(id);
	}
}

int pwm_get_enabled(enum pwm_channel ch)
{
	return MEC1322_PWM_CFG(pwm_channels[ch].channel) & 0x1;
}

void pwm_set_duty(enum pwm_channel ch, int percent)
{
	int id = pwm_channels[ch].channel;

	if (percent < 0)
		percent = 0;
	else if (percent > 100)
		percent = 100;

	MEC1322_PWM_ON(id) = percent;
	MEC1322_PWM_OFF(id) = 100 - percent;
}

int pwm_get_duty(enum pwm_channel ch)
{
	return MEC1322_PWM_ON(pwm_channels[ch].channel);
}

inline uint32_t pwm_get_keep_awake_mask(void)
{
	return pwm_keep_awake_mask;
}

static void pwm_configure(int ch, int active_low, int clock_low)
{
	/*
	 * clock_low=0 selects the 48MHz Ring Oscillator source
	 * clock_low=1 selects the 100kHz_Clk source
	 */
	MEC1322_PWM_CFG(ch) = (15 << 3) |    /* Pre-divider = 16 */
			      (active_low ? (1 << 2) : 0) |
			      (clock_low  ? (1 << 1) : 0);
}

static void pwm_init(void)
{
	int i;

	for (i = 0; i < PWM_CH_COUNT; ++i) {
		pwm_configure(pwm_channels[i].channel,
			      pwm_channels[i].flags & PWM_CONFIG_ACTIVE_LOW,
			      pwm_channels[i].flags & PWM_CONFIG_ALT_CLOCK);
		pwm_set_duty(i, 0);
	}
}
DECLARE_HOOK(HOOK_INIT, pwm_init, HOOK_PRIO_DEFAULT);
