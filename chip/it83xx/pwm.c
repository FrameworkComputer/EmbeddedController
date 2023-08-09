/* Copyright 2014 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* PWM control module for IT83xx. */

#include "clock.h"
#include "gpio.h"
#include "hooks.h"
#include "math_util.h"
#include "pwm.h"
#include "pwm_chip.h"
#include "registers.h"
#include "util.h"

#define PWM_CTRX_MIN 100
#define PWM_EC_FREQ 8000000

const struct pwm_ctrl_t pwm_ctrl_regs[] = {
	{ &IT83XX_PWM_DCR0, &IT83XX_PWM_PCSSGL, &IT83XX_GPIO_GPCRA0 },
	{ &IT83XX_PWM_DCR1, &IT83XX_PWM_PCSSGL, &IT83XX_GPIO_GPCRA1 },
	{ &IT83XX_PWM_DCR2, &IT83XX_PWM_PCSSGL, &IT83XX_GPIO_GPCRA2 },
	{ &IT83XX_PWM_DCR3, &IT83XX_PWM_PCSSGL, &IT83XX_GPIO_GPCRA3 },
	{ &IT83XX_PWM_DCR4, &IT83XX_PWM_PCSSGH, &IT83XX_GPIO_GPCRA4 },
	{ &IT83XX_PWM_DCR5, &IT83XX_PWM_PCSSGH, &IT83XX_GPIO_GPCRA5 },
	{ &IT83XX_PWM_DCR6, &IT83XX_PWM_PCSSGH, &IT83XX_GPIO_GPCRA6 },
	{ &IT83XX_PWM_DCR7, &IT83XX_PWM_PCSSGH, &IT83XX_GPIO_GPCRA7 },
};

const struct pwm_ctrl_t2 pwm_clock_ctrl_regs[] = {
	{ &IT83XX_PWM_CTR, &IT83XX_PWM_C0CPRS, &IT83XX_PWM_C0CPRS,
	  &IT83XX_PWM_PCFSR, 0x01 },
	{ &IT83XX_PWM_CTR1, &IT83XX_PWM_C4CPRS, &IT83XX_PWM_C4MCPRS,
	  &IT83XX_PWM_PCFSR, 0x02 },
	{ &IT83XX_PWM_CTR2, &IT83XX_PWM_C6CPRS, &IT83XX_PWM_C6MCPRS,
	  &IT83XX_PWM_PCFSR, 0x04 },
	{ &IT83XX_PWM_CTR3, &IT83XX_PWM_C7CPRS, &IT83XX_PWM_C7MCPRS,
	  &IT83XX_PWM_PCFSR, 0x08 },
};

static int pwm_get_cycle_time(enum pwm_channel ch)
{
	int pcs_shift;
	int pcs_mask;
	int pcs_reg;
	int cycle_time_setting;

	/* pwm channel mapping */
	ch = pwm_channels[ch].channel;

	/* bit shift for "Prescaler Clock Source Select Group" register. */
	pcs_shift = (ch % 4) * 2;

	/* setting of "Prescaler Clock Source Select Group" register. */
	pcs_reg = *pwm_ctrl_regs[ch].pwm_clock_source;

	/* only bit0 bit1 information. */
	pcs_mask = (pcs_reg >> pcs_shift) & 0x03;

	/* get cycle time setting of PWM channel x. */
	cycle_time_setting = *pwm_clock_ctrl_regs[pcs_mask].pwm_cycle_time;

	return cycle_time_setting;
}

void pwm_enable(enum pwm_channel ch, int enabled)
{
	/* pwm channel mapping */
	int pwm_reg_index = pwm_channels[ch].channel;

	/*
	 * enabled : pin to PWM function.
	 * disabled : pin to GPIO input function.
	 */
	if (enabled)
		*pwm_ctrl_regs[pwm_reg_index].pwm_pin = 0x00;
	else
		*pwm_ctrl_regs[pwm_reg_index].pwm_pin =
			0x80 |
			((pwm_channels[ch].flags & PWM_CONFIG_ACTIVE_LOW) ? 4 :
									    2);
}

int pwm_get_enabled(enum pwm_channel ch)
{
	/* pwm channel mapping */
	ch = pwm_channels[ch].channel;

	/* pin is PWM function and PWMs clock counter was enabled */
	return ((*pwm_ctrl_regs[ch].pwm_pin & ~0x04) == 0x00 &&
		IT83XX_PWM_ZTIER & 0x02) ?
		       1 :
		       0;
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

	if (pwm_channels[ch].flags & PWM_CONFIG_ACTIVE_LOW)
		percent = 100 - percent;

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
	int ch_idx;

	ch_idx = ch;

	/* pwm channel mapping */
	ch = pwm_channels[ch].channel;

	/* setting of "Prescaler Clock Source Select Group" register.*/
	pcs_reg = *pwm_ctrl_regs[ch].pwm_clock_source;

	/* only bit0 bit1 information. */
	pcs_mask = (pcs_reg >> ((ch % 4) * 2)) & 0x03;

	/* get cycle time setting of PWM channel x. */
	cycle_time_setting = *pwm_clock_ctrl_regs[pcs_mask].pwm_cycle_time;

	percent = *pwm_ctrl_regs[ch].pwm_duty * 100 / cycle_time_setting;

	if (pwm_channels[ch_idx].flags & PWM_CONFIG_ACTIVE_LOW)
		percent = 100 - percent;

	/* output signal duty cycle. */
	return percent;
}

void pwm_duty_inc(enum pwm_channel ch)
{
	int cycle_time, pwm_ch;

	/* pwm channel mapping */
	pwm_ch = pwm_channels[ch].channel;

	cycle_time = pwm_get_cycle_time(ch);

	if (pwm_channels[ch].flags & PWM_CONFIG_ACTIVE_LOW) {
		if (*pwm_ctrl_regs[pwm_ch].pwm_duty > 0)
			*pwm_ctrl_regs[pwm_ch].pwm_duty -= 1;
	} else {
		if (*pwm_ctrl_regs[pwm_ch].pwm_duty < cycle_time)
			*pwm_ctrl_regs[pwm_ch].pwm_duty += 1;
	}
}

void pwm_duty_reduce(enum pwm_channel ch)
{
	int cycle_time, pwm_ch;

	/* pwm channel mapping */
	pwm_ch = pwm_channels[ch].channel;

	cycle_time = pwm_get_cycle_time(ch);

	if (pwm_channels[ch].flags & PWM_CONFIG_ACTIVE_LOW) {
		if (*pwm_ctrl_regs[pwm_ch].pwm_duty < cycle_time)
			*pwm_ctrl_regs[pwm_ch].pwm_duty += 1;
	} else {
		if (*pwm_ctrl_regs[pwm_ch].pwm_duty > 0)
			*pwm_ctrl_regs[pwm_ch].pwm_duty -= 1;
	}
}

static int pwm_ch_freq(enum pwm_channel ch)
{
	int actual_freq = -1, targe_freq, deviation;
	int pcfsr, ctr, pcfsr_sel, pcs_shift, pcs_mask;
	int pwm_clk_src = (pwm_channels[ch].flags & PWM_CONFIG_DSLEEP) ?
				  32768 :
				  PWM_EC_FREQ;

	targe_freq = pwm_channels[ch].freq_hz;
	deviation = (targe_freq / 100) + 1;

	for (ctr = 0xFF; ctr >= PWM_CTRX_MIN; ctr--) {
		pcfsr = (pwm_clk_src / (ctr + 1) / targe_freq) - 1;
		if (pcfsr >= 0) {
			actual_freq = pwm_clk_src / (ctr + 1) / (pcfsr + 1);
			if (ABS(actual_freq - targe_freq) < deviation)
				break;
		}
	}

	if (ctr < PWM_CTRX_MIN) {
		actual_freq = -1;
	} else {
		pcfsr_sel = pwm_channels[ch].pcfsr_sel;
		*pwm_clock_ctrl_regs[pcfsr_sel].pwm_cycle_time = ctr;

		if (pwm_channels[ch].flags & PWM_CONFIG_DSLEEP)
			/*
			 * Select 32.768KHz as PWM clock source.
]			 *
			 * NOTE:
			 * For pwm_channels[], the maximum supported pwm output
			 * signal frequency is 324 Hz (32768/(PWM_CTRX_MIN+1)).
			 */
			*pwm_clock_ctrl_regs[pcfsr_sel].pwm_pcfsr_reg &=
				~pwm_clock_ctrl_regs[pcfsr_sel].pwm_pcfsr_ctrl;
		else
			/* ec clock 8MHz */
			*pwm_clock_ctrl_regs[pcfsr_sel].pwm_pcfsr_reg |=
				pwm_clock_ctrl_regs[pcfsr_sel].pwm_pcfsr_ctrl;

		/* pwm channel mapping */
		ch = pwm_channels[ch].channel;

		/*
		 * bit shift for "Prescaler Clock Source Select Group"
		 * register.
		 */
		pcs_shift = (ch % 4) * 2;
		pcs_mask = pcfsr_sel << pcs_shift;

		*pwm_ctrl_regs[ch].pwm_clock_source &= ~(0x3 << pcs_shift);
		*pwm_ctrl_regs[ch].pwm_clock_source |= pcs_mask;

		*pwm_clock_ctrl_regs[pcfsr_sel].pwm_cpr_lsb = pcfsr & 0xFF;
		*pwm_clock_ctrl_regs[pcfsr_sel].pwm_cpr_msb = (pcfsr >> 8) &
							      0xFF;
	}

	return actual_freq;
}

static void pwm_init(void)
{
	int ch;

	for (ch = 0; ch < PWM_CH_COUNT; ch++) {
		pwm_ch_freq(ch);
		pwm_set_duty(ch, 0);
	}

	/*
	 * The cycle timer1 of chip 8320 later series was enhanced from
	 * 8bits to 10bits resolution, and others are still 8bit resolution.
	 * Because the cycle timer1 high byte default value is not zero,
	 * we clear cycle timer1 high byte at init and use it as 8-bit
	 * resolution like others.
	 */
	IT83XX_PWM_CTR1M = 0;
	/* enable PWMs clock counter. */
	IT83XX_PWM_ZTIER |= 0x02;
}

/* The chip PWM module initialization. */
DECLARE_HOOK(HOOK_INIT, pwm_init, HOOK_PRIO_INIT_PWM);
