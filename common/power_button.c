/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Power button module for Chrome EC */

#include "common.h"
#include "console.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "keyboard_scan.h"
#include "lid_switch.h"
#include "power_button.h"
#include "task.h"
#include "timer.h"
#include "util.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_SWITCH, outstr)
#define CPRINTS(format, args...) cprints(CC_SWITCH, format, ## args)

/* By default the power button is active low */
#ifndef CONFIG_POWER_BUTTON_ACTIVE_STATE
#define CONFIG_POWER_BUTTON_ACTIVE_STATE 0
#endif

#define PWRBTN_DEBOUNCE_US (30 * MSEC)  /* Debounce time for power button */

static int debounced_power_pressed;	/* Debounced power button state */
static int simulate_power_pressed;
static volatile int power_button_is_stable = 1;

/**
 * Return non-zero if power button signal asserted at hardware input.
 *
 */
int power_button_signal_asserted(void)
{
	return !!(gpio_get_level(GPIO_POWER_BUTTON_L)
		 == CONFIG_POWER_BUTTON_ACTIVE_STATE);
}

/**
 * Get raw power button signal state.
 *
 * @return 1 if power button is pressed, 0 if not pressed.
 */
static int raw_power_button_pressed(void)
{
	if (simulate_power_pressed)
		return 1;

#ifndef CONFIG_POWER_BUTTON_IGNORE_LID
	/*
	 * Always indicate power button released if the lid is closed.
	 * This prevents waking the system if the device is squashed enough to
	 * press the power button through the closed lid.
	 */
	if (!lid_is_open())
		return 0;
#endif

	return power_button_signal_asserted();
}

int power_button_is_pressed(void)
{
	return debounced_power_pressed;
}

/**
 * Wait for the power button to be released
 *
 * @param timeout_us Timeout in microseconds, or -1 to wait forever
 * @return EC_SUCCESS if ok, or
 *         EC_ERROR_TIMEOUT if power button failed to release
 */
int power_button_wait_for_release(unsigned int timeout_us)
{
	timestamp_t deadline;
	timestamp_t now = get_time();

	deadline.val = now.val + timeout_us;

	while (!power_button_is_stable || power_button_is_pressed()) {
		now = get_time();
		if (timeout_us < 0) {
			task_wait_event(-1);
		} else if (timestamp_expired(deadline, &now) ||
			(task_wait_event(deadline.val - now.val) ==
			TASK_EVENT_TIMER)) {
			CPRINTS("power button not released in time");
			return EC_ERROR_TIMEOUT;
		}
	}

	CPRINTS("power button released in time");
	return EC_SUCCESS;
}

/**
 * Handle power button initialization.
 */
static void power_button_init(void)
{
	if (raw_power_button_pressed())
		debounced_power_pressed = 1;

	/* Enable interrupts, now that we've initialized */
	gpio_enable_interrupt(GPIO_POWER_BUTTON_L);
}
DECLARE_HOOK(HOOK_INIT, power_button_init, HOOK_PRIO_INIT_POWER_BUTTON);

/**
 * Handle debounced power button changing state.
 */
static void power_button_change_deferred(void)
{
	const int new_pressed = raw_power_button_pressed();

	/* Re-enable keyboard scanning if power button is no longer pressed */
	if (!new_pressed)
		keyboard_scan_enable(1, KB_SCAN_DISABLE_POWER_BUTTON);

	/* If power button hasn't changed state, nothing to do */
	if (new_pressed == debounced_power_pressed) {
		power_button_is_stable = 1;
		return;
	}

	debounced_power_pressed = new_pressed;
	power_button_is_stable = 1;

	CPRINTS("power button %s", new_pressed ? "pressed" : "released");

	/* Call hooks */
	hook_notify(HOOK_POWER_BUTTON_CHANGE);

	/* Notify host if power button has been pressed */
	if (new_pressed)
		host_set_single_event(EC_HOST_EVENT_POWER_BUTTON);
}
DECLARE_DEFERRED(power_button_change_deferred);

void power_button_interrupt(enum gpio_signal signal)
{
	/*
	 * If power button is pressed, disable the matrix scan as soon as
	 * possible to reduce the risk of false-reboot triggered by those keys
	 * on the same column with refresh key.
	 */
	if (raw_power_button_pressed())
		keyboard_scan_enable(0, KB_SCAN_DISABLE_POWER_BUTTON);

	/* Reset power button debounce time */
	power_button_is_stable = 0;
	hook_call_deferred(power_button_change_deferred, PWRBTN_DEBOUNCE_US);
}

/*****************************************************************************/
/* Console commands */

static int command_powerbtn(int argc, char **argv)
{
	int ms = 200;  /* Press duration in ms */
	char *e;

	if (argc > 1) {
		ms = strtoi(argv[1], &e, 0);
		if (*e)
			return EC_ERROR_PARAM1;
	}

	ccprintf("Simulating %d ms power button press.\n", ms);
	simulate_power_pressed = 1;
	power_button_is_stable = 0;
	hook_call_deferred(power_button_change_deferred, 0);

	msleep(ms);

	ccprintf("Simulating power button release.\n");
	simulate_power_pressed = 0;
	power_button_is_stable = 0;
	hook_call_deferred(power_button_change_deferred, 0);

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(powerbtn, command_powerbtn,
			"[msec]",
			"Simulate power button press",
			NULL);

