/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* PWM control module for IT83xx. */

#include "clock.h"
#include "fan.h"
#include "gpio.h"
#include "hooks.h"
#include "pwm.h"
#include "pwm_chip.h"
#include "registers.h"
#include "util.h"

const struct pwm_ctrl_t pwm_ctrl_regs[] = {
	{ &IT83XX_PWM_DCR0, &IT83XX_PWM_PCSSGL, &IT83XX_GPIO_GPCRA0},
	{ &IT83XX_PWM_DCR1, &IT83XX_PWM_PCSSGL, &IT83XX_GPIO_GPCRA1},
	{ &IT83XX_PWM_DCR2, &IT83XX_PWM_PCSSGL, &IT83XX_GPIO_GPCRA2},
	{ &IT83XX_PWM_DCR3, &IT83XX_PWM_PCSSGL, &IT83XX_GPIO_GPCRA3},
	{ &IT83XX_PWM_DCR4, &IT83XX_PWM_PCSSGH, &IT83XX_GPIO_GPCRA4},
	{ &IT83XX_PWM_DCR5, &IT83XX_PWM_PCSSGH, &IT83XX_GPIO_GPCRA5},
	{ &IT83XX_PWM_DCR6, &IT83XX_PWM_PCSSGH, &IT83XX_GPIO_GPCRA6},
	{ &IT83XX_PWM_DCR7, &IT83XX_PWM_PCSSGH, &IT83XX_GPIO_GPCRA7},
};

const struct pwm_ctrl_t2 pwm_clock_ctrl_regs[] = {
	{ &IT83XX_PWM_CTR, &IT83XX_PWM_C0CPRS, &IT83XX_PWM_C0CPRS,
		&IT83XX_PWM_PCFSR, 0x01},
	{ &IT83XX_PWM_CTR1, &IT83XX_PWM_C4CPRS, &IT83XX_PWM_C4MCPRS,
		&IT83XX_PWM_PCFSR, 0x02},
	{ &IT83XX_PWM_CTR2, &IT83XX_PWM_C6CPRS, &IT83XX_PWM_C6MCPRS,
		&IT83XX_PWM_PCFSR, 0x04},
	{ &IT83XX_PWM_CTR3, &IT83XX_PWM_C7CPRS, &IT83XX_PWM_C7MCPRS,
		&IT83XX_PWM_PCFSR, 0x08},
};

void pwm_enable(enum pwm_channel ch, int enabled)
{
	/* pwm channel mapping */
	ch = pwm_channels[ch].channel;

	/*
	 * enabled : pin to PWM function.
	 * disabled : pin to GPIO input function.
	 */
	if (enabled)
		*pwm_ctrl_regs[ch].pwm_pin = 0x00;
	else
		*pwm_ctrl_regs[ch].pwm_pin = 0x80;
}

int pwm_get_enabled(enum pwm_channel ch)
{
	/* pwm channel mapping */
	ch = pwm_channels[ch].channel;

	/* pin is PWM function and PWMs clock counter was enabled */
	return ((*pwm_ctrl_regs[ch].pwm_pin & ~0x04) == 0x00 &&
		IT83XX_PWM_ZTIER & 0x02) ? 1 : 0;
}

void pwm_set_duty(enum pwm_channel ch, int percent)
{
	int pcs_shift;
	int pcs_mask;
	int pcs_reg;
	int cycle_time_setting;

	if (percent < 0)
		percent = 0;
	else if (percent > 100)
		percent = 100;

	/* pwm channel mapping */
	ch = pwm_channels[ch].channel;

	/* bit shift for "Prescaler Clock Source Select Group" register. */
	pcs_shift = (ch % 4) * 2;

	/* setting of "Prescaler Clock Source Select Group" register.*/
	pcs_reg = *pwm_ctrl_regs[ch].pwm_clock_source;

	/* only bit0 bit1 information. */
	pcs_mask = (pcs_reg >> pcs_shift) & 0x03;

	/* get cycle time setting of PWM channel x. */
	cycle_time_setting = *pwm_clock_ctrl_regs[pcs_mask].pwm_cycle_time;

	if (pwm_channels[ch].flags & PWM_CONFIG_ACTIVE_LOW)
		percent = 100 - percent;

	/* to update PWM DCRx depend on CTRx setting. */
	if (percent == 100) {
		*pwm_ctrl_regs[ch].pwm_duty = cycle_time_setting;
	} else {
		*pwm_ctrl_regs[ch].pwm_duty =
			((cycle_time_setting + 1) * percent) / 100;
	}
}

int pwm_get_duty(enum pwm_channel ch)
{
	int pcs_mask;
	int pcs_reg;
	int cycle_time_setting;
	int percent;

	/* pwm channel mapping */
	ch = pwm_channels[ch].channel;

	/* setting of "Prescaler Clock Source Select Group" register.*/
	pcs_reg = *pwm_ctrl_regs[ch].pwm_clock_source;

	/* only bit0 bit1 information. */
	pcs_mask = (pcs_reg >> ((ch % 4) * 2)) & 0x03;

	/* get cycle time setting of PWM channel x. */
	cycle_time_setting = *pwm_clock_ctrl_regs[pcs_mask].pwm_cycle_time;

	percent = *pwm_ctrl_regs[ch].pwm_duty * 100 / cycle_time_setting;

	if (pwm_channels[ch].flags & PWM_CONFIG_ACTIVE_LOW)
		percent = 100 - percent;

	/* output signal duty cycle. */
	return percent;
}

static void pwm_init(void)
{
	/* enable PWMs clock counter. */
	IT83XX_PWM_ZTIER |= 0x02;

	/* 0.5 resolution */
	IT83XX_PWM_CTR = 200;
	IT83XX_PWM_CTR1 = 200;
	IT83XX_PWM_CTR2 = 200;
	IT83XX_PWM_CTR3 = 200;
}

/* The chip PWM module initialization. */
DECLARE_HOOK(HOOK_INIT, pwm_init, HOOK_PRIO_DEFAULT);
