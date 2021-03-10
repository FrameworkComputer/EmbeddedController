/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* PWM control module for MCHP MEC family */

#include "common.h"
#include "console.h"
#include "gpio.h"
#include "hooks.h"
#include "pwm.h"
#include "pwm_chip.h"
#include "registers.h"
#include "util.h"
#include "tfdp_chip.h"

#define CPUTS(outstr) cputs(CC_PWM, outstr)
#define CPRINTS(format, args...) cprints(CC_PWM, format, ## args)

/* Bit map of PWM channels that must remain active during low power idle. */
static uint32_t pwm_keep_awake_mask;

/* Table of PWM PCR sleep enable register index and bit position. */
static const uint16_t pwm_pcr[] = {
	MCHP_PCR_PWM0,
	MCHP_PCR_PWM1,
	MCHP_PCR_PWM2,
	MCHP_PCR_PWM3,
	MCHP_PCR_PWM4,
	MCHP_PCR_PWM5,
	MCHP_PCR_PWM6,
	MCHP_PCR_PWM7,
	MCHP_PCR_PWM8,
};
BUILD_ASSERT(ARRAY_SIZE(pwm_pcr) == MCHP_PWM_ID_MAX);

void pwm_enable(enum pwm_channel ch, int enabled)
{
	int id = pwm_channels[ch].channel;

	if (enabled) {
		MCHP_PWM_CFG(id) |= BIT(0);
		if (pwm_channels[ch].flags & PWM_CONFIG_DSLEEP)
			pwm_keep_awake_mask |= BIT(id);
	} else {
		MCHP_PWM_CFG(id) &= ~BIT(0);
		pwm_keep_awake_mask &= ~BIT(id);
	}
}

int pwm_get_enabled(enum pwm_channel ch)
{
	return MCHP_PWM_CFG(pwm_channels[ch].channel) & 0x1;
}

void pwm_set_duty(enum pwm_channel ch, int percent)
{
	int id = pwm_channels[ch].channel;

	if (percent < 0)
		percent = 0;
	else if (percent > 100)
		percent = 100;

	MCHP_PWM_ON(id) = percent;
	MCHP_PWM_OFF(id) = 100 - percent;
}

int pwm_get_duty(enum pwm_channel ch)
{
	return MCHP_PWM_ON(pwm_channels[ch].channel);
}

void pwm_keep_awake(void)
{
	if (pwm_keep_awake_mask) {
		for (uint32_t i = 0; i < MCHP_PWM_ID_MAX; i++)
			if (pwm_keep_awake_mask & BIT(i))
				MCHP_PCR_SLP_DIS_DEV(pwm_pcr[i]);
	} else {
		MCHP_PCR_SLOW_CLK_CTL &= ~(MCHP_PCR_SLOW_CLK_CTL_MASK);
	}
}

/*
 * clock_low=0 selects the 48MHz Ring Oscillator source
 * clock_low=1 selects the 100kHz_Clk source
 */
static void pwm_configure(int ch, int active_low, int clock_low)
{
	MCHP_PWM_CFG(ch) = (15 << 3) /* divider = 16 */
		| (active_low ? BIT(2) : 0)
		| (clock_low  ? BIT(1) : 0);
}

static void pwm_slp_en(int pwm_id, int sleep_en)
{
	if ((pwm_id < 0) || (pwm_id > MCHP_PWM_ID_MAX))
		return;

	if (sleep_en)
		MCHP_PCR_SLP_EN_DEV(pwm_pcr[pwm_id]);
	else
		MCHP_PCR_SLP_DIS_DEV(pwm_pcr[pwm_id]);
}

static void pwm_init(void)
{
	int i;

	for (i = 0; i < PWM_CH_COUNT; ++i) {
		pwm_slp_en(pwm_channels[i].channel, 0);
		pwm_configure(pwm_channels[i].channel,
			      pwm_channels[i].flags & PWM_CONFIG_ACTIVE_LOW,
			      pwm_channels[i].flags & PWM_CONFIG_ALT_CLOCK);
		pwm_set_duty(i, 0);
	}
}
DECLARE_HOOK(HOOK_INIT, pwm_init, HOOK_PRIO_DEFAULT);
