/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* GAIA SoC power sequencing module for Chrome EC */

#include "board.h"
#include "chipset.h"  /* This module implements chipset functions too */
#include "console.h"
#include "gpio.h"
#include "task.h"
#include "timer.h"
#include "util.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_CHIPSET, outstr)
#define CPRINTF(format, args...) cprintf(CC_CHIPSET, format, ## args)

/* Time necessary for the 5v regulator output to stabilize */
#define DELAY_5V_SETUP        1000  /* 1ms */

/* Delay between 1.35v and 3.3v rails startup */
#define DELAY_RAIL_STAGGERING 100  /* 100us */

/* Long power key press to force shutdown */
#define DELAY_FORCE_SHUTDOWN  8000000 /* 8s */

/* debounce time to prevent accidental power-on after keyboard power off */
#define KB_PWR_ON_DEBOUNCE    250    /* 250us */

/* PMIC fails to set the LDO2 output */
#define PMIC_TIMEOUT          100000  /* 100ms */

/* Default timeout for input transition */
#define FAIL_TIMEOUT          500000 /* 500ms */


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
			(task_wait_event(deadline.val - now.val) ==
			 TASK_EVENT_TIMER)) {
			CPRINTF("Timeout waiting for GPIO %d\n", signal);
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
		while ((gpio_get_level(GPIO_KB_PWR_ON_L) == 1) &&
			(gpio_get_level(GPIO_SOC1V8_XPSHOLD) == 1)) {
				task_wait_event(-1);
		}

		/* XPSHOLD released by AP : shutdown immediatly */
		if (gpio_get_level(GPIO_SOC1V8_XPSHOLD) == 0)
			return;

		/* relay to PMIC */
		gpio_set_level(GPIO_PMIC_PWRON_L, 0);

		/* check if power button is pressed for 8s */
		deadline.val = get_time().val + DELAY_FORCE_SHUTDOWN;
		while ((gpio_get_level(GPIO_KB_PWR_ON_L) == 0) &&
			(gpio_get_level(GPIO_SOC1V8_XPSHOLD) == 1)) {
			now = get_time();
			if ((now.val >= deadline.val) ||
				(task_wait_event(deadline.val - now.val) ==
				 TASK_EVENT_TIMER)) {
					gpio_set_level(GPIO_PMIC_PWRON_L, 1);
					return;
			}
		}

		gpio_set_level(GPIO_PMIC_PWRON_L, 1);

		/*
		 * Holding down the power button causes this loop to spin
		 * endlessly, triggering the watchdog. So add a wait here.
		 */
		task_wait_event(-1);
	}
}

void gaia_power_event(enum gpio_signal signal)
{
	/* Wake up the task */
	task_wake(TASK_ID_GAIAPOWER);
}

int gaia_power_init(void)
{
	/* Enable interrupts for our GPIOs */
	gpio_enable_interrupt(GPIO_KB_PWR_ON_L);
	gpio_enable_interrupt(GPIO_PP1800_LDO2);
	gpio_enable_interrupt(GPIO_SOC1V8_XPSHOLD);

	return EC_SUCCESS;
}


/*****************************************************************************/
/* Chipset interface */

int chipset_in_state(int state_mask)
{
	/* If AP is off, match any off state for now */
	if ((state_mask & CHIPSET_STATE_ANY_OFF) && !ap_on)
		return 1;

	/* If AP is on, match on state */
	if ((state_mask & CHIPSET_STATE_ON) && ap_on)
		return 1;

	/* TODO: detect suspend state */

	/* In any other case, we don't have a match */
	return 0;
}


void chipset_exit_hard_off(void)
{
	/* TODO: implement, if/when we take the AP down to a hard-off state */
}

/*****************************************************************************/

void gaia_power_task(void)
{
	gaia_power_init();

	while (1) {
		/* Power OFF state */
		ap_on = 0;

		/* wait for Power button press */
		wait_in_signal(GPIO_KB_PWR_ON_L, 0, -1);

		usleep(KB_PWR_ON_DEBOUNCE);
		if (gpio_get_level(GPIO_KB_PWR_ON_L) == 1)
			continue;

		/* Enable 5v power rail */
		gpio_set_level(GPIO_EN_PP5000, 1);
		/* wait to have stable power */
		usleep(DELAY_5V_SETUP);

		/* Startup PMIC */
		gpio_set_level(GPIO_PMIC_PWRON_L, 0);
		/* wait for all PMIC regulators to be ready */
		wait_in_signal(GPIO_PP1800_LDO2, 1, PMIC_TIMEOUT);

		/* if PP1800_LDO2 did not come up (e.g. PMIC_TIMEOUT was
		 * reached), turn off 5v rail and start over */
		if (gpio_get_level(GPIO_PP1800_LDO2) == 0) {
			gpio_set_level(GPIO_EN_PP5000, 0);
			usleep(DELAY_5V_SETUP);
			continue;
		}

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
		gpio_set_level(GPIO_PMIC_PWRON_L, 1);

		/* Power ON state */
		ap_on = 1;
		CPUTS("AP running ...\n");

		/* Wait for power off from AP or long power button press */
		wait_for_power_off();
		/* switch off all rails */
		gpio_set_level(GPIO_EN_PP3300, 0);
		gpio_set_level(GPIO_EN_PP1350, 0);
		gpio_set_level(GPIO_EN_PP5000, 0);
		CPUTS("Shutdown complete.\n");

		/* Ensure the power button is released */
		wait_in_signal(GPIO_KB_PWR_ON_L, 1, -1);
	}
}

/*****************************************************************************/
/* Console debug command */

static int command_force_power(int argc, char **argv)
{
	/* simulate power button pressed */
	force_signal = GPIO_KB_PWR_ON_L;
	force_value = 1;
	/* Wake up the task */
	task_wake(TASK_ID_GAIAPOWER);
	/* wait 100 ms */
	usleep(100000);
	/* release power button */
	force_signal = -1;
	force_value = 0;

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(forcepower, command_force_power);
