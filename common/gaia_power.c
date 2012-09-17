/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * GAIA SoC power sequencing module for Chrome EC
 *
 * This implements the following features:
 *
 * - Cold reset powers off the AP
 *
 *  When powered off:
 *  - Press pwron turns on the AP
 *  - Hold pwron turns on the AP, and then 16s later turns it off and leaves
 *  it off until pwron is released and pressed again
 *
 *  When powered on:
 *  - The PMIC PWRON signal is released <= 1 second after the power button is
 *  released (we expect that U-Boot as asserted XPSHOLD by then)
 *  - Holding pwron for 8s powers off the AP
 *  - Pressing and releasing pwron within that 8s is ignored
 *  - If XPSHOLD is dropped by the AP, then we power the AP off
 */

#include "clock.h"
#include "chipset.h"  /* This module implements chipset functions too */
#include "common.h"
#include "console.h"
#include "gpio.h"
#include "hooks.h"
#include "keyboard_scan.h"
#include "power_led.h"
#include "pmu_tpschrome.h"
#include "system.h"
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

/*
 * If the power key is pressed to turn on, then held for this long, we
 * power off.
 *
 * The idea here is that behavior for 8s for AP shutdown is unchanged
 * but power-on is modified to allow enough time U-Boot to be updated
 * via USB (which takes about 10sec).
 *
 * So after power button is pressed:

 * Normal case: User releases power button and gaia_power_task() goes
 *    into the inner loop, waiting for next event to occur (power button
 *    press or XPSHOLD == 0).
 *
 * U-Boot updating: User presses and holds power button. If EC does not
 *    see XPSHOLD, it waits up to 16sec for an event. If no event occurs
 *    within 16sec, EC powers off AP.
 */
#define DELAY_SHUTDOWN_ON_POWER_HOLD	(8 * 1000000)
#define DELAY_SHUTDOWN_ON_USB_BOOT      (16 * 1000000)

/* Maximum delay after power button press before we release GPIO_PMIC_PWRON_L */
#define DELAY_RELEASE_PWRON	1000000 /* 1s */

/* debounce time to prevent accidental power-on after keyboard power off */
#define KB_PWR_ON_DEBOUNCE    250    /* 250us */

/* debounce time to prevent accidental power event after lid open/close */
#define LID_SWITCH_DEBOUNCE   250    /* 250us */

/* PMIC fails to set the LDO2 output */
#define PMIC_TIMEOUT          100000  /* 100ms */

/* Default timeout for input transition */
#define FAIL_TIMEOUT          500000 /* 500ms */


/* Application processor power state */
static int ap_on;
static int ap_suspended;

/* simulated event state */
static int force_signal = -1;
static int force_value;

/* 1 if the power button was pressed last time we checked */
static char power_button_was_pressed;

/* 1 if a change in lid switch state has been detected */
static char lid_changed;

/* time where we will power off, if power button still held down */
static timestamp_t power_off_deadline;

/* force AP power on (used for recovery keypress) */
static int auto_power_on;

enum power_request_t {
	POWER_REQ_NONE,
	POWER_REQ_OFF,
	POWER_REQ_ON,

	POWER_REQ_COUNT,
};

static enum power_request_t power_request;

/*
 * Wait for GPIO "signal" to reach level "value".
 * Returns EC_ERROR_TIMEOUT if timeout before reaching the desired state.
 *
 * @param signal	Signal to watch
 * @param value		Value to watch for
 * @param timeout	Timeout in microseconds from now, or -1 to wait forever
 * @return 0 if signal did change to required value, EC_ERROR_TIMEOUT if we
 * timed out first.
 */
static int wait_in_signal(enum gpio_signal signal, int value, int timeout)
{
	timestamp_t deadline;
	timestamp_t now = get_time();

	deadline.val = now.val + timeout;

	while (((force_signal != signal) || (force_value != value)) &&
			gpio_get_level(signal) != value) {
		now = get_time();
		if (timeout < 0) {
			task_wait_event(-1);
		} else if (timestamp_expired(deadline, &now) ||
				(task_wait_event(deadline.val - now.val) ==
					TASK_EVENT_TIMER)) {
			CPRINTF("Timeout waiting for GPIO %d/%s\n", signal,
				    gpio_get_name(signal));
			return EC_ERROR_TIMEOUT;
		}
	}

	return EC_SUCCESS;
}

/*
 * Check for some event triggering the shutdown.
 *
 * It can be either a long power button press or a shutdown triggered from the
 * AP and detected by reading XPSHOLD.
 *
 * @return 1 if a shutdown should happen, 0 if not
 */
static int check_for_power_off_event(void)
{
	timestamp_t now;
	int pressed = 0;

	/* Check for power button press */
	if (gpio_get_level(GPIO_KB_PWR_ON_L) == 0) {
		udelay(KB_PWR_ON_DEBOUNCE);
		if (gpio_get_level(GPIO_KB_PWR_ON_L) == 0)
			pressed = 1;
	}

	/* Dis/Enable keyboard scanning when the power button state changes */
	if (!pressed || pressed != power_button_was_pressed)
		keyboard_enable_scanning(!pressed);


	now = get_time();
	if (pressed) {
		gpio_set_level(GPIO_PMIC_PWRON_L, 0);

		if (!power_button_was_pressed) {
			power_off_deadline.val = now.val + DELAY_FORCE_SHUTDOWN;
			CPRINTF("Waiting for long power press %u\n",
				power_off_deadline.le.lo);
		} else if (timestamp_expired(power_off_deadline, &now)) {
			power_off_deadline.val = 0;
			CPRINTF("Power off after long press now=%u, %u\n",
				now.le.lo, power_off_deadline.le.lo);
			return 2;
		}
	} else if (power_button_was_pressed) {
		CPUTS("Cancel power off\n");
		gpio_set_level(GPIO_PMIC_PWRON_L, 1);
	}

	power_button_was_pressed = pressed;

	/* XPSHOLD released by AP : shutdown immediately */
	if (gpio_get_level(GPIO_SOC1V8_XPSHOLD) == 0)
		return 3;

	if (power_request == POWER_REQ_OFF) {
		power_request = POWER_REQ_NONE;
		return 4;
	}

	return 0;
}

void gaia_suspend_event(enum gpio_signal signal)
{
	if (!ap_on) /* power on/off : not a real suspend / resume */
		return;

	/*
	 * Note: For Snow, suspend state can only be reliably
	 * determined when the AP is on (crosbug.com/p/13200).
	 */
	ap_suspended = !gpio_get_level(GPIO_SUSPEND_L);

	if (ap_suspended) {
		if (gpio_get_level(GPIO_LID_OPEN))
			powerled_set_state(POWERLED_STATE_SUSPEND);
		else
			powerled_set_state(POWERLED_STATE_OFF);
		/* Call hooks here since we don't know it prior to AP suspend */
		hook_notify(HOOK_CHIPSET_SUSPEND, 0);
	} else {
		powerled_set_state(POWERLED_STATE_ON);
		hook_notify(HOOK_CHIPSET_RESUME, 0);
	}
}

void gaia_power_event(enum gpio_signal signal)
{
	/* Wake up the task */
	task_wake(TASK_ID_GAIAPOWER);
}

void gaia_lid_event(enum gpio_signal signal)
{
	/* inform power task that lid switch has changed */
	lid_changed = 1;
	task_wake(TASK_ID_GAIAPOWER);
}

int gaia_power_init(void)
{
	/* Enable interrupts for our GPIOs */
	gpio_enable_interrupt(GPIO_KB_PWR_ON_L);
	gpio_enable_interrupt(GPIO_LID_OPEN);
	gpio_enable_interrupt(GPIO_PP1800_LDO2);
	gpio_enable_interrupt(GPIO_SOC1V8_XPSHOLD);
	gpio_enable_interrupt(GPIO_SUSPEND_L);

	/* Leave power off only if requested by reset flags */
	if (!(system_get_reset_flags() & RESET_FLAG_AP_OFF))
		auto_power_on = 1;

	/* Auto power on if the recovery combination was pressed */
	if (keyboard_scan_recovery_pressed())
		auto_power_on = 1;

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
	if ((state_mask & CHIPSET_STATE_ON) && ap_on && !ap_suspended)
		return 1;

	/* if AP is suspended, match on state */
	if ((state_mask & CHIPSET_STATE_SUSPEND) && ap_on && ap_suspended)
		return 1;

	/* In any other case, we don't have a match */
	return 0;
}

void chipset_exit_hard_off(void)
{
	/* TODO: implement, if/when we take the AP down to a hard-off state */
}

/*****************************************************************************/

/**
 * Check if there has been a power-on event
 *
 * This checks all power-on event signals and returns boolean 0 or 1 to
 * indicate if any have been triggered (with debounce taken into account).
 *
 * @return 1 if there has been a power-on event, 0 if not
 */
static int check_for_power_on_event(void)
{
	/* the system is already ON */
	if (gpio_get_level(GPIO_EN_PP3300))
		return 1;

	/* power on requested at EC startup for recovery */
	if (auto_power_on) {
		auto_power_on = 0;
		return 2;
	}

	/* to avoid false positives, check lid only if a change was detected */
	if (lid_changed) {
		udelay(LID_SWITCH_DEBOUNCE);
		if (gpio_get_level(GPIO_LID_OPEN) == 1) {
			lid_changed = 0;
			return 3;
		}
	}

	/* check for power button press */
	if (gpio_get_level(GPIO_KB_PWR_ON_L) == 0) {
		udelay(KB_PWR_ON_DEBOUNCE);
		if (gpio_get_level(GPIO_KB_PWR_ON_L) == 0)
			return 4;
	}

	if (power_request == POWER_REQ_ON) {
		power_request = POWER_REQ_NONE;
		return 5;
	}

	return 0;
}

/**
 * Power on the AP
 *
 * @return 0 if ok, -1 on error (PP1800_LDO2 failed to come on)
 */
static int power_on(void)
{
	/* Enable 5v power rail */
	gpio_set_level(GPIO_EN_PP5000, 1);
	/* wait to have stable power */
	usleep(DELAY_5V_SETUP);

	if (gpio_get_level(GPIO_SOC1V8_XPSHOLD) == 0) {
		/* Initialize non-AP components */
		hook_notify(HOOK_CHIPSET_PRE_INIT, 0);

		/*
		 * Initiate PMIC power-on sequence only if cold booting AP to
		 * avoid accidental reset (crosbug.com/p/12650).
		 */
		gpio_set_level(GPIO_PMIC_PWRON_L, 0);
	}

	/* wait for all PMIC regulators to be ready */
	wait_in_signal(GPIO_PP1800_LDO2, 1, PMIC_TIMEOUT);

	/*
	 * If PP1800_LDO2 did not come up (e.g. PMIC_TIMEOUT was reached),
	 * turn off 5v rail and start over.
	 */
	if (gpio_get_level(GPIO_PP1800_LDO2) == 0) {
		gpio_set_level(GPIO_EN_PP5000, 0);
		usleep(DELAY_5V_SETUP);
		CPUTS("Fatal error: PMIC failed to enable\n");
		return -1;
	}

	/* Enable DDR 1.35v power rail */
	gpio_set_level(GPIO_EN_PP1350, 1);
	/* wait to avoid large inrush current */
	usleep(DELAY_RAIL_STAGGERING);
	/* Enable 3.3v power rail */
	gpio_set_level(GPIO_EN_PP3300, 1);
	ap_on = 1;
	disable_sleep(SLEEP_MASK_AP_RUN);
	powerled_set_state(POWERLED_STATE_ON);

	/* Call hooks now that AP is running */
	hook_notify(HOOK_CHIPSET_STARTUP, 0);

	CPRINTF("[%T AP running ...]\n");
	return 0;
}

/**
 * Wait for the power button to be released
 *
 * @return 0 if ok, -1 if power button failed to release
 */
static int wait_for_power_button_release(unsigned int timeout_us)
{
	/* wait for Power button release */
	wait_in_signal(GPIO_KB_PWR_ON_L, 1, timeout_us);

	udelay(KB_PWR_ON_DEBOUNCE);
	if (gpio_get_level(GPIO_KB_PWR_ON_L) == 0) {
		CPUTS("Power button was not released in time\n");
		return -1;
	}
	CPUTS("Power button released\n");
	return 0;
}

/**
 * Wait for the XPSHOLD signal from the AP to be asserted within timeout_us
 * and if asserted clear the PMIC_PWRON signal
 *
 * @return 0 if ok, -1 if power button failed to release
 */
static int react_to_xpshold(unsigned int timeout_us)
{
	/* wait for Power button release */
	wait_in_signal(GPIO_SOC1V8_XPSHOLD, 1, timeout_us);

	if (gpio_get_level(GPIO_SOC1V8_XPSHOLD) == 0) {
		CPUTS("XPSHOLD not seen in time\n");
		return -1;
	}
	CPRINTF("%T XPSHOLD seen\n");
	gpio_set_level(GPIO_PMIC_PWRON_L, 1);
	return 0;
}

/**
 * Power off the AP
 */
static void power_off(void)
{
	int pmu_shutdown_retries = 3;

	/* Call hooks before we drop power rails */
	hook_notify(HOOK_CHIPSET_SHUTDOWN, 0);
	/* switch off all rails */
	gpio_set_level(GPIO_EN_PP3300, 0);
	gpio_set_level(GPIO_EN_PP1350, 0);
	gpio_set_level(GPIO_PMIC_PWRON_L, 1);
	gpio_set_level(GPIO_EN_PP5000, 0);
	ap_on = 0;
	ap_suspended = 0;
	lid_changed = 0;
	enable_sleep(SLEEP_MASK_AP_RUN);
	powerled_set_state(POWERLED_STATE_OFF);

	while (--pmu_shutdown_retries >= 0) {
		if (!pmu_shutdown())
			break;
	}
	if (pmu_shutdown_retries < 0)
		board_hard_reset();
	CPUTS("Shutdown complete.\n");
}


/*
 * Calculates the delay in microseconds to the next time we have to check
 * for a power event,
 *
  *@return delay to next check, or -1 if no future check is needed
 */
static int next_pwr_event(void)
{
	if (!power_off_deadline.val)
		return -1;

	return power_off_deadline.val - get_time().val;
}


/*****************************************************************************/

void gaia_power_task(void)
{
	int value;

	gaia_power_init();
	ap_on = 0;

	while (1) {
		/* Wait until we need to power on, then power on */
		while (value = check_for_power_on_event(), !value)
			task_wait_event(-1);
		CPRINTF("%T power on %d\n", value);

		if (!power_on()) {
			int continue_power = 0;

			if (!react_to_xpshold(DELAY_RELEASE_PWRON)) {
				/* AP looks good */
				if (!wait_for_power_button_release(
					DELAY_SHUTDOWN_ON_POWER_HOLD))
					continue_power = 1;
			} else {
				/* AP is possibly in bad shape */
				/* allow USB boot in 16 secs */
				if (!wait_for_power_button_release(
					DELAY_SHUTDOWN_ON_USB_BOOT))
					continue_power = 1;
			}
			if (continue_power) {
				power_button_was_pressed = 0;
				while (!(value = check_for_power_off_event()))
					task_wait_event(next_pwr_event());
				CPRINTF("%T ending loop %d\n", value);
			}
		}
		power_off();
		wait_for_power_button_release(-1);
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
DECLARE_CONSOLE_COMMAND(forcepower, command_force_power,
			NULL,
			"Force power on",
			NULL);

static const char *power_req_name[POWER_REQ_COUNT] = {
	"none",
	"off",
	"on",
};

/* Power states that we can report */
enum power_state_t {
	PSTATE_UNKNOWN,
	PSTATE_OFF,
	PSTATE_SUSPEND,
	PSTATE_ON,

	PSTATE_COUNT,
};

static const char * const state_name[] = {
	"unknown",
	"off",
	"suspend",
	"on",
};

static int command_power(int argc, char **argv)
{
	if (argc < 2) {
		enum power_state_t state;

		state = PSTATE_UNKNOWN;
		if (chipset_in_state(CHIPSET_STATE_ANY_OFF))
			state = PSTATE_OFF;
		if (chipset_in_state(CHIPSET_STATE_SUSPEND))
			state = PSTATE_SUSPEND;
		if (chipset_in_state(CHIPSET_STATE_ON))
			state = PSTATE_ON;
		ccprintf("%s\n", state_name[state]);

		return EC_SUCCESS;
	}

	if (0 == strcasecmp(argv[1], "on"))
		power_request = POWER_REQ_ON;
	else if (0 == strcasecmp(argv[1], "off"))
		power_request = POWER_REQ_OFF;
	else
		return EC_ERROR_PARAM1;

	ccprintf("[%T PB Requesting power %s]\n", power_req_name[power_request]);
	task_wake(TASK_ID_GAIAPOWER);

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(power, command_power,
			"on/off",
			"Turn AP power on/off",
			NULL);

void system_warm_reboot(void)
{
	CPRINTF("[%T EC triggered warm reboot ]\n");

	/* This is a hack to do an AP warm reboot while still preserving
	 * RAM contents. This is useful for looking at kernel log message
	 * contents from previous boot in cases where the AP/OS is hard
	 * hung. */
	gpio_set_level(GPIO_EN_PP5000, 0);
	gpio_set_level(GPIO_EN_PP3300, 0);

	power_request = POWER_REQ_ON;
	task_wake(TASK_ID_GAIAPOWER);

	return;
}

static int command_warm_reboot(int argc, char **argv)
{
	system_warm_reboot();
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(warm_reboot, command_warm_reboot,
			NULL,
			"EC triggered warm reboot",
			NULL);
