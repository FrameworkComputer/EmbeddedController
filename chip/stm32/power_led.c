/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * Keyboard power button LED state machine.
 *
 * This sets up TIM_POWER_LED to drive the power button LED so that the duty
 * cycle can range from 0-100%. When the lid is closed or turned off, then the
 * PWM is disabled and the GPIO is reconfigured to minimize leakage voltage.
 *
 * In suspend mode, duty cycle transitions progressively slower from 0%
 * to 100%, and progressively faster from 100% back down to 0%. This
 * results in a breathing effect. It takes about 2sec for a full cycle.
 */

#include "clock.h"
#include "console.h"
#include "gpio.h"
#include "hooks.h"
#include "hwtimer.h"
#include "power_led.h"
#include "registers.h"
#include "task.h"
#include "timer.h"
#include "util.h"

#define LED_STATE_TIMEOUT_MIN	(15 * MSEC)  /* Minimum of 15ms per step */
#define LED_HOLD_TIME		(330 * MSEC) /* Hold for 330ms at min/max */
#define LED_STEP_PERCENT	4	/* Incremental value of each step */

static enum powerled_state led_state = POWERLED_STATE_ON;
static int power_led_percent = 100;
static int using_pwm;

void powerled_set_state(enum powerled_state new_state)
{
	led_state = new_state;
	/* Wake up the task */
	task_wake(TASK_ID_POWERLED);
}

static void power_led_set_duty(int percent)
{
	ASSERT((percent >= 0) && (percent <= 100));
	power_led_percent = percent;
	/*
	 * Set the duty cycle.  CCRx = percent * ARR / 100.  Since we set
	 * ARR=100, this is just percent.
	 */
#ifdef BOARD_snow
	STM32_TIM_CCR2(TIM_POWER_LED) = percent;
#else
	STM32_TIM_CCR3(TIM_POWER_LED) = percent;
#endif
}

static void power_led_use_pwm(void)
{
	/* Configure power LED GPIO for TIM2/PWM alternate function */
#ifdef BOARD_snow
	/* PB3 = TIM2_CH2 */
	uint32_t val = STM32_GPIO_CRL(GPIO_B) & ~0x0000f000;
	val |= 0x00009000;	/* alt. function (TIM2/PWM) */
	STM32_GPIO_CRL(GPIO_B) = val;
#else
	gpio_config_module(MODULE_POWER_LED, 1);
#endif

	/* Enable timer */
	__hw_timer_enable_clock(TIM_POWER_LED, 1);

	/* Disable counter during setup */
	STM32_TIM_CR1(TIM_POWER_LED) = 0x0000;

	/*
	 * CPU clock / PSC determines how fast the counter operates.
	 * ARR determines the wave period, CCRn determines duty cycle.
	 * Thus, frequency = cpu_freq / PSC / ARR. so:
	 *
	 *     frequency = cpu_freq / (cpu_freq/10000) / 100 = 100 Hz.
	 */
	STM32_TIM_PSC(TIM_POWER_LED) = clock_get_freq() / 10000;
	STM32_TIM_ARR(TIM_POWER_LED) = 100;

	power_led_set_duty(100);

#ifdef BOARD_snow
	/* CC2 configured as output, PWM mode 1, preload enable */
	STM32_TIM_CCMR1(TIM_POWER_LED) = (6 << 12) | (1 << 11);

	/* CC2 output enable, active low */
	STM32_TIM_CCER(TIM_POWER_LED) = (1 << 4) | (1 << 5);
#else
	/* CC3 configured as output, PWM mode 1, preload enable */
	STM32_TIM_CCMR2(TIM_POWER_LED) = (6 << 4) | (1 << 3);

	/* CC3 output enable, active low */
	STM32_TIM_CCER(TIM_POWER_LED) = (1 << 8) | (1 << 9);
#endif

	/* Generate update event to force loading of shadow registers */
	STM32_TIM_EGR(TIM_POWER_LED) |= 1;

	/* Enable auto-reload preload, start counting */
	STM32_TIM_CR1(TIM_POWER_LED) |= (1 << 7) | (1 << 0);

	using_pwm = 1;
}

static void power_led_manual_off(void)
{
	/* Disable counter */
	STM32_TIM_CR1(TIM_POWER_LED) &= ~0x1;

	/* Disable timer clock */
	__hw_timer_enable_clock(TIM_POWER_LED, 0);

	/*
	 * Reconfigure GPIO as a floating input. Alternatively we could
	 * configure it as an open-drain output and set it to high impedence,
	 * but reconfiguring as an input had better results in testing.
	 */
#ifdef BOARD_snow
	gpio_set_flags(GPIO_LED_POWER_L, GPIO_INPUT);
	gpio_set_level(GPIO_LED_POWER_L, 1);
#else
	gpio_config_module(MODULE_POWER_LED, 0);
#endif

	using_pwm = 0;
}

/**
 * Return the timeout period (in us) for the current step.
 */
static int power_led_step(void)
{
	int state_timeout = 0;
	static enum { DOWN = -1, UP = 1 } dir = UP;

	if (0 == power_led_percent) {
		dir = UP;
		state_timeout = LED_HOLD_TIME;
	} else if (100 == power_led_percent) {
		dir = DOWN;
		state_timeout = LED_HOLD_TIME;
	} else {
		/*
		 * Decreases timeout as duty cycle percentage approaches
		 * 0%, increase as it approaches 100%.
		 */
		state_timeout = LED_STATE_TIMEOUT_MIN +
			LED_STATE_TIMEOUT_MIN * (power_led_percent / 33);
	}

	/*
	 * The next duty cycle will take effect after the timeout has
	 * elapsed for this duty cycle and the power LED task calls this
	 * function again.
	 */
	power_led_set_duty(power_led_percent);
	power_led_percent += dir * LED_STEP_PERCENT;

	return state_timeout;
}

/**
 * Handle clock frequency change
 */
static void power_led_freq_change(void)
{
	/* If we're using PWM, re-initialize to adjust timer divisor */
	if (using_pwm)
		power_led_use_pwm();
}
DECLARE_HOOK(HOOK_FREQ_CHANGE, power_led_freq_change, HOOK_PRIO_DEFAULT);

void power_led_task(void)
{
	while (1) {
		int state_timeout = -1;

		switch (led_state) {
		case POWERLED_STATE_ON:
			/*
			 * "ON" implies driving the LED using the PWM with a
			 * duty duty cycle of 100%. This produces a softer
			 * brightness than setting the GPIO to solid ON.
			 */
			if (!using_pwm)
				power_led_use_pwm();
			power_led_set_duty(100);
			state_timeout = -1;
			break;
		case POWERLED_STATE_OFF:
			/* Reconfigure GPIO to disable the LED */
			if (using_pwm)
				power_led_manual_off();
			state_timeout = -1;
			break;
		case POWERLED_STATE_SUSPEND:
			/* Drive using PWM with variable duty cycle */
			if (!using_pwm)
				power_led_use_pwm();
			state_timeout = power_led_step();
			break;
		default:
			break;
		}

		task_wait_event(state_timeout);
	}
}

#ifdef CONFIG_CMD_POWERLED
static int command_powerled(int argc, char **argv)
{
	enum powerled_state state;

	if (argc != 2)
		return EC_ERROR_INVAL;

	if (!strcasecmp(argv[1], "off"))
		state = POWERLED_STATE_OFF;
	else if (!strcasecmp(argv[1], "on"))
		state = POWERLED_STATE_ON;
	else if (!strcasecmp(argv[1], "suspend"))
		state = POWERLED_STATE_SUSPEND;
	else
		return EC_ERROR_INVAL;

	powerled_set_state(state);
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(powerled, command_powerled,
		"[off | on | suspend]",
		"Change power LED state",
		NULL);
#endif
