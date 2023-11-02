/* Copyright 2013 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* PWM control module for STM32 */

#include "builtin/assert.h"
#include "clock.h"
#include "clock_chip.h"
#include "gpio.h"
#include "hooks.h"
#include "hwtimer.h"
#include "pwm.h"
#include "pwm_chip.h"
#include "registers.h"
#include "system.h"
#include "util.h"

/* Bitmap of currently active PWM channels. 1 bit per channel. */
static atomic_t using_pwm;

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
	timer_ctlr_t *tim = (timer_ctlr_t *)(pwm->tim.base);
	volatile unsigned int *ccmr = NULL;
	/* Default frequency = 100 Hz */
	int frequency = pwm->frequency ? pwm->frequency : 100;
	uint16_t ccer;

	if (using_pwm & BIT(ch))
		return;

	/* Enable timer */
	__hw_timer_enable_clock(pwm->tim.id, 1);

	/* Disable counter during setup */
	tim->cr1 = 0x0000;

	/*
	 * Timer clock / PSC determines how fast the counter operates.
	 * ARR determines the wave period, CCRn determines duty cycle.
	 * Thus, frequency = timer_freq / PSC / ARR. so:
	 *
	 * frequency = timer_freq / (timer_freq/10000 + 1) / (99 + 1) = 100 Hz.
	 */
	tim->psc = clock_get_timer_freq() / (frequency * 100) - 1;
	tim->arr = 99;

	if (pwm->channel <= 2) /* Channel ID starts from 1 */
		ccmr = &tim->ccmr1;
	else
		ccmr = &tim->ccmr2;

	/* Output, PWM mode 1, preload enable */
	if (pwm->channel & 0x1)
		*ccmr = (6 << 4) | BIT(3);
	else
		*ccmr = (6 << 12) | BIT(11);

	/* Output enable. Set active high/low. */
	if (pwm->flags & PWM_CONFIG_ACTIVE_LOW)
		ccer = 3 << (pwm->channel * 4 - 4);
	else
		ccer = 1 << (pwm->channel * 4 - 4);

	/* Enable complementary output, if present. */
	if (pwm->flags & PWM_CONFIG_COMPLEMENTARY_OUTPUT)
		ccer |= (ccer << 2);

	tim->ccer = ccer;

	/*
	 * Main output enable.
	 * TODO(shawnn): BDTR is undocumented on STM32L. Verify this isn't
	 * harmful on STM32L.
	 */
	tim->bdtr |= BIT(15);

	/* Generate update event to force loading of shadow registers */
	tim->egr |= 1;

	/* Enable auto-reload preload, start counting */
	tim->cr1 |= BIT(7) | BIT(0);

	atomic_or(&using_pwm, 1 << ch);

	/* Prevent sleep */
	disable_sleep(SLEEP_MASK_PWM);
}

static void pwm_disable(enum pwm_channel ch)
{
	const struct pwm_t *pwm = pwm_channels + ch;
	timer_ctlr_t *tim = (timer_ctlr_t *)(pwm->tim.base);

	if ((using_pwm & BIT(ch)) == 0)
		return;

	/* Main output disable */
	tim->bdtr &= ~BIT(15);

	/* Disable counter */
	tim->cr1 &= ~0x1;

	/* Disable timer clock */
	__hw_timer_enable_clock(pwm->tim.id, 0);

	/* Allow sleep */
	enable_sleep(SLEEP_MASK_PWM);

	atomic_clear_bits(&using_pwm, 1 << ch);

	/* Unless another PWM is active... Then prevent sleep */
	if (using_pwm)
		disable_sleep(SLEEP_MASK_PWM);
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
	return using_pwm & BIT(ch);
}

static void pwm_reconfigure(enum pwm_channel ch)
{
	atomic_clear_bits(&using_pwm, 1 << ch);
	pwm_configure(ch);
}

/**
 * Handle clock frequency change
 */
static void pwm_freq_change(void)
{
	int i;
	for (i = 0; i < PWM_CH_COUNT; ++i)
		if (pwm_get_enabled(i))
			pwm_reconfigure(i);
}
DECLARE_HOOK(HOOK_FREQ_CHANGE, pwm_freq_change, HOOK_PRIO_DEFAULT);
