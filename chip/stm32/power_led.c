/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * Keyboard power button LED state machine.
 *
 * This sets up TIM2 to drive the power button LED so that the duty cycle
 * can range from 0-100%. When the lid is closed or turned off, then the
 * PWM is disabled and the GPIO is reconfigured to minimize leakage voltage.
 *
 * In suspend mode, duty cycle transitions progressively slower from 0%
 * to 100%, and progressively faster from 100% back down to 0%. This
 * results in a breathing effect. It takes about 2sec for a full cycle.
 */

#include "console.h"
#include "power_led.h"
#include "registers.h"
#include "task.h"
#include "timer.h"
#include "util.h"

#define LED_STATE_TIMEOUT_MIN	15000	/* minimum of 15ms per step */
#define LED_HOLD_TIME		330000	/* hold for 330ms at min/max */
#define LED_STEP_PERCENT	4	/* incremental value of each step */

static enum powerled_state led_state = POWERLED_STATE_ON;
static enum powerled_config led_config = POWERLED_CONFIG_MANUAL_OFF;
static int power_led_percent = 100;

void powerled_set_state(enum powerled_state new_state)
{
	led_state = new_state;
	/* Wake up the task */
	task_wake(TASK_ID_POWERLED);
}

/* set board-level power LED config options (e.g. manual off/on, PWM) */
void board_power_led_config(enum powerled_state config)
		__attribute__((weak, alias("__board_power_led_config")));

/* Provide a default function in case the board doesn't have one */
void __board_power_led_config(enum powerled_config config)
{
}

static void power_led_use_pwm(void)
{
	board_power_led_config(POWERLED_CONFIG_PWM);

	/* enable TIM2 clock */
	STM32_RCC_APB1ENR |= 0x1;

	/* disable counter during setup */
	STM32_TIM_CR1(2) = 0x0000;

	/*
	 * CPU_CLOCK / PSC determines how fast the counter operates.
	 * ARR determines the wave period, CCRn determines duty cycle.
	 * Thus, frequency = CPU_CLOCK / PSC / ARR.
	 *
	 * Assuming 16MHz clock, the following yields:
	 * 16MHz / 1600 / 100 = 100Hz.
	 */
	STM32_TIM_PSC(2) = CPU_CLOCK / 10000;	/* pre-scaler */
	STM32_TIM_ARR(2) = 100;			/* auto-reload value */
	STM32_TIM_CCR2(2) = 100;		/* duty cycle */

	/* CC2 configured as output, PWM mode 1, preload enable */
	STM32_TIM_CCMR1(2) = (6 << 12) | (1 << 11);

	/* CC2 output enable, active low */
	STM32_TIM_CCER(2) = (1 << 4) | (1 << 5);

	/* generate update event to force loading of shadow registers */
	STM32_TIM_EGR(2) |= 1;

	/* enable auto-reload preload, start counting */
	STM32_TIM_CR1(2) |= (1 << 7) | (1 << 0);

	led_config = POWERLED_CONFIG_PWM;
}

static void power_led_manual_off(void)
{
	/* disable counter */
	STM32_TIM_CR1(2) &= ~0x1;

	/* disable TIM2 clock */
	STM32_RCC_APB1ENR &= ~0x1;

	board_power_led_config(POWERLED_CONFIG_MANUAL_OFF);
	led_config = POWERLED_CONFIG_MANUAL_OFF;
}

static void power_led_set_duty(int percent)
{
	ASSERT((percent >= 0) && (percent <= 100));
	power_led_percent = percent;
	STM32_TIM_CCR2(2) = (STM32_TIM_ARR(2) / 100) * percent;
}

/* returns the timeout period (in us) for current step */
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
		 * 0%, increase as it appraoches 100%.
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
			if (led_config != POWERLED_CONFIG_PWM)
				power_led_use_pwm();
			power_led_set_duty(100);
			state_timeout = -1;
			break;
		case POWERLED_STATE_OFF:
			/* reconfigure GPIO to disable the LED */
			if (led_config != POWERLED_CONFIG_MANUAL_OFF)
				power_led_manual_off();
			state_timeout = -1;
			break;
		case POWERLED_STATE_SUSPEND:
			/* drive using PWM with variable duty cycle */
			if (led_config != POWERLED_CONFIG_PWM)
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
	enum powerled_state state = POWERLED_STATE_OFF;

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
		"[off | on | suspend ]",
		"Change power LED state",
		NULL);
#endif
