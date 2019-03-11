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

/*
 * PWMs that must remain active in low-power idle -
 * PWM 0,1-8 are b[4,20:27] of MCHP_PCR_SLP_EN1
 * PWM 9 is b[31] of MCHP_PCR_SLP_EN3
 * PWM 10 - 11 are b[0:1] of MCHP_PCR_SLP_EN4
 * store 32-bit word with
 * b[0:1] = PWM 10-11
 * b[4,20:27] = PWM 0, 1-8
 * b[31] = PWM 9
 */
static uint32_t pwm_keep_awake_mask;

const uint8_t pwm_slp_bitpos[12] = {
	4, 20, 21, 22, 23, 24, 25, 26, 27, 31, 0, 1
};

static uint32_t pwm_get_sleep_mask(int id)
{
	uint32_t bitpos = 32;

	if (id < 12)
		bitpos = (uint32_t)pwm_slp_bitpos[id];

	return (1ul << bitpos);
}


void pwm_enable(enum pwm_channel ch, int enabled)
{
	int id = pwm_channels[ch].channel;
	uint32_t pwm_slp_mask;

	pwm_slp_mask = pwm_get_sleep_mask(id);

	if (enabled) {
		MCHP_PWM_CFG(id) |= 0x1;
		if (pwm_channels[ch].flags & PWM_CONFIG_DSLEEP)
			pwm_keep_awake_mask |= pwm_slp_mask;
	} else {
		MCHP_PWM_CFG(id) &= ~0x1;
		pwm_keep_awake_mask &= ~pwm_slp_mask;
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
		/* b[4,20:27] */
		MCHP_PCR_SLP_EN1 &= ~(pwm_keep_awake_mask &
					 (MCHP_PCR_SLP_EN1_PWM_ALL));
		/* b[31] */
		MCHP_PCR_SLP_EN3 &= ~(pwm_keep_awake_mask &
					 (MCHP_PCR_SLP_EN3_PWM_ALL));
		/* b[1:0] */
		MCHP_PCR_SLP_EN4 &= ~(pwm_keep_awake_mask &
					 (MCHP_PCR_SLP_EN4_PWM_ALL));
	} else {
		MCHP_PCR_SLOW_CLK_CTL &= 0xFFFFFC00;
	}
}


static void pwm_configure(int ch, int active_low, int clock_low)
{
	/*
	 * clock_low=0 selects the 48MHz Ring Oscillator source
	 * clock_low=1 selects the 100kHz_Clk source
	 */
	MCHP_PWM_CFG(ch) = (15 << 3) |    /* Pre-divider = 16 */
			      (active_low ? BIT(2) : 0) |
			      (clock_low  ? BIT(1) : 0);
}

static const uint16_t pwm_pcr[MCHP_PWM_ID_MAX] = {
	MCHP_PCR_PWM0,
	MCHP_PCR_PWM1,
	MCHP_PCR_PWM2,
	MCHP_PCR_PWM3,
	MCHP_PCR_PWM4,
	MCHP_PCR_PWM5,
	MCHP_PCR_PWM6,
	MCHP_PCR_PWM7,
	MCHP_PCR_PWM8,
	MCHP_PCR_PWM9,
	MCHP_PCR_PWM10,
	MCHP_PCR_PWM11,
};

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
