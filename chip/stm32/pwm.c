/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* PWM control module for STM32 */

#include "clock.h"
#include "gpio.h"
#include "hooks.h"
#include "hwtimer.h"
#include "pwm.h"
#include "pwm_chip.h"
#include "registers.h"
#include "util.h"

static int using_pwm[PWM_CH_COUNT];

void pwm_set_duty(enum pwm_channel ch, int percent)
{
	const struct pwm_t *pwm = pwm_channels + ch;
	timer_ctlr_t *tim = (timer_ctlr_t *)(pwm->tim.base);

	ASSERT((percent >= 0) && (percent <= 100));
	tim->ccr[pwm->channel] = percent;
}

int pwm_get_duty(enum pwm_channel ch)
{
	const struct pwm_t *pwm = pwm_channels + ch;
	timer_ctlr_t *tim = (timer_ctlr_t *)(pwm->tim.base);
	return tim->ccr[pwm->channel];
}

static void pwm_configure(enum pwm_channel ch)
{
	const struct pwm_t *pwm = pwm_channels + ch;
	const struct gpio_info *gpio = gpio_list + pwm->pin;
	timer_ctlr_t *tim = (timer_ctlr_t *)(pwm->tim.base);
	volatile unsigned *ccmr = NULL;
#ifdef CHIP_FAMILY_STM32F
	int mask = gpio->mask;
	volatile uint32_t *gpio_cr = NULL;
	uint32_t val;
#endif

	if (using_pwm[ch])
		return;

#if defined(CHIP_FAMILY_STM32F)
	if (mask < 0x100) {
		gpio_cr = &STM32_GPIO_CRL(gpio->port);
	} else {
		gpio_cr = &STM32_GPIO_CRH(gpio->port);
		mask >>= 8;
	}

	/* Expand mask from 8-bit to 32-bit */
	mask = mask * mask;
	mask = mask * mask;

	/* Set alternate function */
	val = *gpio_cr & ~(mask * 0xf);
	val |= mask * 0x9;
	*gpio_cr = val;
#elif defined(CHIP_FAMILY_STM32F0) || defined(CHIP_FAMILY_STM32F3)
	gpio_set_alternate_function(gpio->port, gpio->mask, pwm->gpio_alt_func);
#else /* stm32l */
	gpio_set_alternate_function(gpio->port, gpio->mask,
				    GPIO_ALT_TIM(pwm->tim.id));
#endif

	/* Enable timer */
	__hw_timer_enable_clock(pwm->tim.id, 1);

	/* Disable counter during setup */
	tim->cr1 = 0x0000;

	/*
	 * CPU clock / PSC determines how fast the counter operates.
	 * ARR determines the wave period, CCRn determines duty cycle.
	 * Thus, frequency = cpu_freq / PSC / ARR. so:
	 *
	 *     frequency = cpu_freq / (cpu_freq/10000 + 1) / (99 + 1) = 100 Hz.
	 */
	tim->psc = clock_get_freq() / 10000 - 1;
	tim->arr = 99;

	if (pwm->channel <= 2) /* Channel ID starts from 1 */
		ccmr = &tim->ccmr1;
	else
		ccmr = &tim->ccmr2;

	/* Output, PWM mode 1, preload enable */
	if (pwm->channel & 0x1)
		*ccmr = (6 << 4) | (1 << 3);
	else
		*ccmr = (6 << 12) | (1 << 11);

	/* Output enable. Set active high/low. */
	if (pwm->flags & PWM_CONFIG_ACTIVE_LOW)
		tim->ccer = 3 << (pwm->channel * 4 - 4);
	else
		tim->ccer = 1 << (pwm->channel * 4 - 4);

	/*
	 * Main output enable.
	 * TODO(shawnn): BDTR is undocumented on STM32L. Verify this isn't
	 * harmful on STM32L.
	 */
	tim->bdtr |= (1 << 15);

	/* Generate update event to force loading of shadow registers */
	tim->egr |= 1;

	/* Enable auto-reload preload, start counting */
	tim->cr1 |= (1 << 7) | (1 << 0);

	using_pwm[ch] = 1;
}

static void pwm_disable(enum pwm_channel ch)
{
	const struct pwm_t *pwm = pwm_channels + ch;
	timer_ctlr_t *tim = (timer_ctlr_t *)(pwm->tim.base);

	if (using_pwm[ch] == 0)
		return;

	/* Main output disable */
	tim->bdtr &= ~(1 << 15);

	/* Disable counter */
	tim->cr1 &= ~0x1;

	/* Disable timer clock */
	__hw_timer_enable_clock(pwm->tim.id, 0);

	using_pwm[ch] = 0;
}

void pwm_enable(enum pwm_channel ch, int enabled)
{
	if (enabled)
		pwm_configure(ch);
	else
		pwm_disable(ch);
}

int pwm_get_enabled(enum pwm_channel ch)
{
	return using_pwm[ch];
}

static void pwm_reconfigure(enum pwm_channel ch)
{
	using_pwm[ch] = 0;
	pwm_configure(ch);
}

/**
 * Handle clock frequency change
 */
static void pwm_freq_change(void)
{
	int i;
	for (i = 0; i < PWM_CH_COUNT; ++i)
		if (using_pwm[i])
			pwm_reconfigure(i);
}
DECLARE_HOOK(HOOK_FREQ_CHANGE, pwm_freq_change, HOOK_PRIO_DEFAULT);
