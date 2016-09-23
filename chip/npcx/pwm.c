/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* PWM control module for NPCX.
 *
 * On this chip, the PWM logic is implemented by the hardware FAN modules.
 */

#include "assert.h"
#include "clock.h"
#include "clock_chip.h"
#include "console.h"
#include "ec_commands.h"
#include "fan.h"
#include "gpio.h"
#include "hooks.h"
#include "pwm.h"
#include "pwm_chip.h"
#include "registers.h"
#include "util.h"

#if !(DEBUG_PWM)
#define CPRINTS(...)
#else
#define CPRINTS(format, args...) cprints(CC_PWM, format, ## args)
#endif

/* pwm resolution for each channel */
static uint32_t pwm_res[PWM_CH_COUNT];

/* PWM clock source */
enum npcx_pwm_source_clock {
	NPCX_PWM_CLOCK_APB2_LFCLK  = 0,
	NPCX_PWM_CLOCK_FX          = 1,
	NPCX_PWM_CLOCK_FR          = 2,
	NPCX_PWM_CLOCK_RESERVED    = 3,
	NPCX_PWM_CLOCK_UNDEF       = 0xFF
};

/* PWM heartbeat mode */
enum npcx_pwm_heartbeat_mode {
	NPCX_PWM_HBM_NORMAL    = 0,
	NPCX_PWM_HBM_25        = 1,
	NPCX_PWM_HBM_50        = 2,
	NPCX_PWM_HBM_100       = 3,
	NPCX_PWM_HBM_UNDEF     = 0xFF
};

/**
 * Set PWM operation clock.
 *
 * @param   ch      operation channel
 * @param   freq    desired PWM frequency
 * @notes   changed when initialization
 */
static void pwm_set_freq(enum pwm_channel ch, uint32_t freq)
{
	int mdl = pwm_channels[ch].channel;
	uint32_t clock;
	uint32_t pre;

	assert(freq != 0);

	/* Disable PWM for module configuration */
	pwm_enable(ch, 0);

	/*
	 * Get PWM clock frequency. Use internal 32K as PWM clock source if
	 * the PWM must be active during low-power idle.
	 */
	if (pwm_channels[ch].flags & PWM_CONFIG_DSLEEP)
		clock = INT_32K_CLOCK;
	else
		clock = clock_get_apb2_freq();

	/* Calculate prescaler */
	pre = DIV_ROUND_UP(clock, (0xffff * freq));

	/* Calculate maximum resolution for the given freq. and prescaler */
	pwm_res[ch] = (clock / pre) / freq;

	/* Make sure we have at least 1% resolution */
	assert(pwm_res[ch] >= 100);

	/* Set PWM prescaler. */
	NPCX_PRSC(mdl) = pre - 1;

	/* Set PWM cycle time */
	NPCX_CTR(mdl) = pwm_res[ch];

	/* Set the duty cycle to 100% since DCR == CTR */
	NPCX_DCR(mdl) = pwm_res[ch];
}

/**
 * Set PWM enabled.
 *
 * @param   ch      operation channel
 * @param   enabled enabled flag
 * @return  none
 */
void pwm_enable(enum pwm_channel ch, int enabled)
{
	int mdl = pwm_channels[ch].channel;

	/* Start or close PWM module */
	UPDATE_BIT(NPCX_PWMCTL(mdl), NPCX_PWMCTL_PWR, enabled);
}

/**
 * Check PWM enabled.
 *
 * @param   ch  operation channel
 * @return  enabled or not
 */
int pwm_get_enabled(enum pwm_channel ch)
{
	int mdl = pwm_channels[ch].channel;
	return IS_BIT_SET(NPCX_PWMCTL(mdl), NPCX_PWMCTL_PWR);
}

/**
 * Set PWM duty cycle.
 *
 * @param   ch      operation channel
 * @param   percent duty cycle percent
 * @return  none
 */
void pwm_set_duty(enum pwm_channel ch, int percent)
{
	/* Convert 16 bit duty to percent on [0, 100] */
	pwm_set_raw_duty(ch, (percent * EC_PWM_MAX_DUTY) / 100);
}

/**
 * Set PWM duty cycle.
 *
 * @param   ch      operation channel
 * @param   duty    cycle duty
 * @return  none
 */
void pwm_set_raw_duty(enum pwm_channel ch, uint16_t duty)
{
	int mdl = pwm_channels[ch].channel;
	uint32_t sd;

	CPRINTS("pwm%d, set duty=%d", mdl, duty);

	/* Assume the fan control is active high and invert it ourselves */
	UPDATE_BIT(NPCX_PWMCTL(mdl), NPCX_PWMCTL_INVP,
			(pwm_channels[ch].flags & PWM_CONFIG_ACTIVE_LOW));

	CPRINTS("freq=0x%x", pwm_channels[ch].freq);
	CPRINTS("duty_cycle_cnt=%d", duty);

	/* duty ranges from 0 - 0xffff, so scale down to 0 - pwm_res[ch] */
	sd = DIV_ROUND_NEAREST(duty * pwm_res[ch], EC_PWM_MAX_DUTY);

	/* Set the duty cycle */
	NPCX_DCR(mdl) = (uint16_t)sd;

	pwm_enable(ch, !!duty);
}

/**
 * Get PWM duty cycle.
 *
 * @param   ch  operation channel
 * @return  duty cycle percent
 */
int pwm_get_duty(enum pwm_channel ch)
{
	/* duty ranges from 0 - 0xffff, so scale to 0 - 100 */
	return DIV_ROUND_NEAREST(pwm_get_raw_duty(ch) * 100, EC_PWM_MAX_DUTY);
}

/**
 * Get PWM duty cycle.
 *
 * @param   ch  operation channel
 * @return  duty cycle
 */
uint16_t pwm_get_raw_duty(enum pwm_channel ch)
{
	int mdl = pwm_channels[ch].channel;

	/* Return duty */
	if (!pwm_get_enabled(ch))
		return 0;
	else
		/*
		 * NPCX_DCR ranges from 0 - pwm_res[ch],
		 * so scale to 0 - 0xffff
		 */
		return DIV_ROUND_NEAREST(NPCX_DCR(mdl) * EC_PWM_MAX_DUTY,
						pwm_res[ch]);
}

/**
 * PWM configuration.
 *
 * @param  ch    operation channel
 * @return none
 */
void pwm_config(enum pwm_channel ch)
{
	int mdl = pwm_channels[ch].channel;

	/* Disable PWM for module configuration */
	pwm_enable(ch, 0);

	/* Set PWM heartbeat mode is no heartbeat */
	SET_FIELD(NPCX_PWMCTL(mdl), NPCX_PWMCTL_HB_DC_CTL_FIELD,
			NPCX_PWM_HBM_NORMAL);

	/* Select default CLK or LFCLK clock input to PWM module */
	SET_FIELD(NPCX_PWMCTLEX(mdl), NPCX_PWMCTLEX_FCK_SEL_FIELD,
			NPCX_PWM_CLOCK_APB2_LFCLK);

	/* Set PWM polarity normal first */
	CLEAR_BIT(NPCX_PWMCTL(mdl), NPCX_PWMCTL_INVP);

	/* Select PWM clock source */
	UPDATE_BIT(NPCX_PWMCTL(mdl), NPCX_PWMCTL_CKSEL,
			(pwm_channels[ch].flags & PWM_CONFIG_DSLEEP));

	/* Set PWM operation frequency */
	pwm_set_freq(ch, pwm_channels[ch].freq);
}

/**
 * PWM initial.
 *
 * @param none
 * @return none
 */
static void pwm_init(void)
{
	int i;
	uint8_t pd_mask = 0;

	/* Take enabled PWMs out of power-down state */
	for (i = 0; i < PWM_CH_COUNT; i++) {
		pd_mask |= (1 << pwm_channels[i].channel);
		pwm_res[i] = 0;
	}

	clock_enable_peripheral(CGC_OFFSET_PWM, pd_mask, CGC_MODE_ALL);

	for (i = 0; i < PWM_CH_COUNT; i++)
		pwm_config(i);
}

/* The chip-specific fan module initializes before this. */
DECLARE_HOOK(HOOK_INIT, pwm_init, HOOK_PRIO_INIT_PWM);
