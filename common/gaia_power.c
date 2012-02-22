/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* GAIA SoC power sequencing module for Chrome EC */

#include "board.h"
#include "console.h"
#include "gpio.h"
#include "task.h"
#include "timer.h"
#include "uart.h"
#include "util.h"

/* Time necessary for the 5v regulator output to stabilize */
#define DELAY_5V_SETUP        1000  /* 1ms */

/* Delay between 1.35v and 3.3v rails startup */
#define DELAY_RAIL_STAGGERING 100  /* 100us */

/* Long power key press to force shutdown */
#define DELAY_FORCE_SHUTDOWN  8000000 /* 8s */

/* PMIC fails to set the LDO2 output */
#define PMIC_TIMEOUT          200000  /* 200ms */

/* Default timeout for input transition */
#define FAIL_TIMEOUT          5000000 /* 5s */


/* Application processor power state */
static int ap_on;

/* simulated event state */
static int force_signal = -1;
static int force_value;

/* Wait for GPIO "signal" to reach level "value".
 * Returns EC_ERROR_TIMEOUT if timeout before reaching the desired state.
 */
static int wait_in_signal(enum gpio_signal signal, int value, int timeout)
{
	timestamp_t deadline;
	timestamp_t now = get_time();

	if (timeout < 0)
		deadline.le.hi = 0xffffffff;
	else
		deadline.val = now.val + timeout;

	while (((force_signal != signal) || (force_value != value)) &&
		gpio_get_level(signal) != value) {
		now = get_time();
		if ((now.val >= deadline.val) ||
			(task_wait_msg(deadline.val - now.val) ==
			 (1 << TASK_ID_TIMER))) {
			uart_printf("Timeout waiting for GPIO %d\n", signal);
			return EC_ERROR_TIMEOUT;
		}
	}

	return EC_SUCCESS;
}

/* Wait for some event triggering the shutdown.
 *
 * It can be either a long power button press or a shutdown triggered from the
 * AP and detected by reading XPSHOLD.
 */
static void wait_for_power_off(void)
{
	timestamp_t deadline, now;

	while (1) {
		/* wait for power button press or XPSHOLD falling edge */
		while ((gpio_get_level(GPIO_EC_PWRON) == 0) &&
			(gpio_get_level(GPIO_SOC1V8_XPSHOLD) == 1)) {
				task_wait_msg(-1);
		}
		/* XPSHOLD released by AP : shutdown immediatly */
		if (gpio_get_level(GPIO_SOC1V8_XPSHOLD) == 0)
			return;

		/* check if power button is pressed for 8s */
		deadline.val = get_time().val + DELAY_FORCE_SHUTDOWN;
		while ((gpio_get_level(GPIO_EC_PWRON) == 1) &&
			(gpio_get_level(GPIO_SOC1V8_XPSHOLD) == 1)) {
			now = get_time();
			if ((now.val >= deadline.val) ||
				(task_wait_msg(deadline.val - now.val) ==
				 (1 << TASK_ID_TIMER)))
					return;
		}
	}
}

void gaia_power_event(enum gpio_signal signal)
{
	/* Wake up the task */
	task_send_msg(TASK_ID_GAIAPOWER, TASK_ID_GAIAPOWER, 0);
}

int gaia_power_init(void)
{
	/* Enable interrupts for our GPIOs */
	gpio_enable_interrupt(GPIO_EC_PWRON);
	gpio_enable_interrupt(GPIO_PP1800_LDO2);
	gpio_enable_interrupt(GPIO_SOC1V8_XPSHOLD);

	return EC_SUCCESS;
}

/* TODO: rename this to something generic */
int x86_power_in_S0(void) {
	return ap_on;
}

void gaia_power_task(void)
{
	gaia_power_init();

	while (1) {
		/* Power OFF state */
		ap_on = 0;

		/* wait for Power button press */
		wait_in_signal(GPIO_EC_PWRON, 1, -1);

		/* Enable 5v power rail */
		gpio_set_level(GPIO_EN_PP5000, 1);
		/* wait to have stable power */
		usleep(DELAY_5V_SETUP);

		/* Startup PMIC */
		gpio_set_level(GPIO_PMIC_ACOK, 1);
		/* wait for all PMIC regulators to be ready */
		wait_in_signal(GPIO_PP1800_LDO2, 1, PMIC_TIMEOUT);

		/* Enable DDR 1.35v power rail */
		gpio_set_level(GPIO_EN_PP1350, 1);
		/* wait to avoid large inrush current */
		usleep(DELAY_RAIL_STAGGERING);
		/* Enable 3.3v power rail */
		gpio_set_level(GPIO_EN_PP3300, 1);

		/* wait for the Application Processor to take control of the
		 * PMIC.
		 */
		wait_in_signal(GPIO_SOC1V8_XPSHOLD, 1, FAIL_TIMEOUT);
		/* release PMIC startup signal */
		gpio_set_level(GPIO_PMIC_ACOK, 0);

		/* Power ON state */
		ap_on = 1;
		uart_printf("AP running ...\n");

		/* Wait for power off from AP or long power button press */
		wait_for_power_off();
		/* switch off all rails */
		gpio_set_level(GPIO_EN_PP3300, 0);
		gpio_set_level(GPIO_EN_PP1350, 0);
		gpio_set_level(GPIO_EN_PP5000, 0);
		uart_printf("Shutdown complete.\n");

		/* Ensure the power button is released */
		wait_in_signal(GPIO_EC_PWRON, 0, -1);
	}
}

/*****************************************************************************/
/* Console debug command */

static int command_force_power(int argc, char **argv)
{
	/* simulate power button pressed */
	force_signal = GPIO_EC_PWRON;
	force_value = 1;
	/* Wake up the task */
	task_send_msg(TASK_ID_GAIAPOWER, TASK_ID_GAIAPOWER, 0);
	/* wait 100 ms */
	usleep(100000);
	/* release power button */
	force_signal = -1;
	force_value = 0;

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(forcepower, command_force_power);
