/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * TEGRA SoC power sequencing module for Chrome EC
 *
 * This implements the following features:
 *
 * - Cold reset powers on the AP
 *
 *  When powered off:
 *  - Press pwron turns on the AP
 *  - Hold pwron turns on the AP, and then 9s later turns it off and leaves
 *    it off until pwron is released and pressed again
 *
 *  When powered on:
 *  - The PMIC PWRON signal is released <= 1 second after the power button is
 *    released
 *  - Holding pwron for 9s powers off the AP
 *  - Pressing and releasing pwron within that 9s is ignored
 *  - If XPSHOLD is dropped by the AP, then we power the AP off
 */

#include "clock.h"
#include "chipset.h"  /* This module implements chipset functions too */
#include "common.h"
#include "console.h"
#include "gpio.h"
#include "hooks.h"
#include "lid_switch.h"
#include "keyboard_scan.h"
#include "power_button.h"
#include "power_led.h"
#include "pmu_tpschrome.h"
#include "system.h"
#include "task.h"
#include "timer.h"
#include "util.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_CHIPSET, outstr)
#define CPRINTF(format, args...) cprintf(CC_CHIPSET, format, ## args)

/* Long power key press to force shutdown */
#define DELAY_FORCE_SHUTDOWN  (10200 * MSEC)  /* 10.2 seconds */

/*
 * The minimum time to assert the PMIC PWRON pin is 20ms.
 * Give it longer to ensure the PMIC doesn't lose it.
 */
#define PMIC_PWRON_DEBOUNCE_TIME  (20 * MSEC * 3)

/*
 * The minimum time to assert the PMIC THERM pin is 32us. However,
 * it needs to be extended to about 50ms to let the 5V rail
 * dissipate fully.
 */
#define PMIC_THERM_HOLD_TIME  (50 * MSEC)

/*
 * If the power key is pressed to turn on, then held for this long, we
 * power off.
 *
 * Normal case: User releases power button and chipset_task() goes
 *    into the inner loop, waiting for next event to occur (power button
 *    press or XPSHOLD == 0).
 */
#define DELAY_SHUTDOWN_ON_POWER_HOLD	(10200 * MSEC)  /* 10.2 seconds */

/* Maximum delay after power button press before we deassert GPIO_PMIC_PWRON */
#define DELAY_RELEASE_PWRON   SECOND /* 1s */

/*
 * nyan's GPIO_SOC1V8_XPSHOLD will go low for ~20ms after initial high.
 * XPSHOLD_DEBOUNCE is used to wait this long, then check the signal again.
 */
#define XPSHOLD_DEBOUNCE      (30 * 1000)  /* 30 ms */

/* Application processor power state */
static int ap_on;
static int ap_suspended;

/* simulated event state */
static int force_signal = -1;
static int force_value;

/* 1 if the power button was pressed last time we checked */
static char power_button_was_pressed;

/* 1 if lid-open event has been detected */
static char lid_opened;

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

/**
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
			CPRINTF("[%T power timeout waiting for GPIO %d/%s]\n",
				signal, gpio_get_name(signal));
			return EC_ERROR_TIMEOUT;
		}
	}

	return EC_SUCCESS;
}

/**
 * Set the PMIC PWRON signal.
 *
 * Note that asserting requires holding for PMIC_PWRON_DEBOUNCE_TIME.
 *
 * @param asserted	Assert (=1) or deassert (=0) the signal.  This is the
 *			logical level of the pin, not the physical level.
 */
static void set_pmic_pwron(int asserted)
{
	/* Signal is active-low */
	gpio_set_level(GPIO_PMIC_PWRON_L, asserted ? 0 : 1);
}

/**
 * Set the PMIC THERM to force shutdown the AP.
 *
 * @param asserted	Assert (=1) or deassert (=0) the signal.  This is the
 *			logical level of the pin, not the physical level.
 */
static void set_pmic_therm(int asserted)
{
	/* Signal is active-low */
	gpio_set_level(GPIO_PMIC_THERM_L, asserted ? 0 : 1);
}

/**
 * Check for some event triggering the shutdown.
 *
 * It can be either a long power button press or a shutdown triggered from the
 * AP and detected by reading XPSHOLD.
 *
 * @return non-zero if a shutdown should happen, 0 if not
 */
static int check_for_power_off_event(void)
{
	timestamp_t now;
	int pressed = 0;

	/*
	 * Check for power button press.
	 */
	if (power_button_is_pressed()) {
		pressed = 1;
	} else if (power_request == POWER_REQ_OFF) {
		power_request = POWER_REQ_NONE;
		return 4;  /* return non-zero for shudown down */
	}

#ifdef HAS_TASK_KEYSCAN
	/* Dis/Enable keyboard scanning when the power button state changes */
	if (!pressed || pressed != power_button_was_pressed)
		keyboard_scan_enable(!pressed);
#endif

	now = get_time();
	if (pressed) {
		set_pmic_pwron(1);
		usleep(PMIC_PWRON_DEBOUNCE_TIME);

		if (!power_button_was_pressed) {
			power_off_deadline.val = now.val + DELAY_FORCE_SHUTDOWN;
			CPRINTF("[%T power waiting for long press %u]\n",
				power_off_deadline.le.lo);
		} else if (timestamp_expired(power_off_deadline, &now)) {
			power_off_deadline.val = 0;
			CPRINTF("[%T power off after long press now=%u, %u]\n",
				now.le.lo, power_off_deadline.le.lo);
			return 2;
		}
	} else if (power_button_was_pressed) {
		CPRINTF("[%T power off cancel]\n");
		set_pmic_pwron(0);
	}

	power_button_was_pressed = pressed;

	/* XPSHOLD released by AP : shutdown immediately */
	if (gpio_get_level(GPIO_SOC1V8_XPSHOLD) == 0)
		return 3;

	return 0;
}

/**
 * Deferred handling for suspend events
 *
 * The suspend event needs to be able to call the suspend and resume hooks.
 * This cannot be done from interrupt level, since the handlers from those
 * hooks may need to use mutexes or other functionality not present at
 * interrupt level.  Use a deferred function instead.
 *
 * Deferred functions are called from the hook task and not the chipset task,
 * so that's a slight deviation from the spec in hooks.h, but a minor one.
 */
static void tegra_suspend_deferred(void)
{
	int new_ap_suspended;

	if (!ap_on) /* power on/off : not a real suspend / resume */
		return;

	new_ap_suspended = !gpio_get_level(GPIO_SUSPEND_L);

	/* We never want to call two suspend or two resumes in a row */
	if (ap_suspended == new_ap_suspended)
		return;

	ap_suspended = new_ap_suspended;

	if (ap_suspended) {
		if (lid_is_open())
			powerled_set_state(POWERLED_STATE_SUSPEND);
		else
			powerled_set_state(POWERLED_STATE_OFF);
		/* Call hooks here since we don't know it prior to AP suspend */
		hook_notify(HOOK_CHIPSET_SUSPEND);
	} else {
		powerled_set_state(POWERLED_STATE_ON);
		hook_notify(HOOK_CHIPSET_RESUME);
	}
}
DECLARE_DEFERRED(tegra_suspend_deferred);

void power_interrupt(enum gpio_signal signal)
{
	if (signal == GPIO_SUSPEND_L) {
		/* Handle suspend events in the hook task */
		hook_call_deferred(tegra_suspend_deferred, 0);
	} else {
		/* All other events are handled in the chipset task */
		task_wake(TASK_ID_CHIPSET);
	}
}

static void tegra_lid_event(void)
{
	/* Power task only cares about lid-open events */
	if (!lid_is_open())
		return;

	lid_opened = 1;
	task_wake(TASK_ID_CHIPSET);
}
DECLARE_HOOK(HOOK_LID_CHANGE, tegra_lid_event, HOOK_PRIO_DEFAULT);

static int tegra_power_init(void)
{
	/* Enable interrupts for our GPIOs */
	gpio_enable_interrupt(GPIO_SOC1V8_XPSHOLD);
	gpio_enable_interrupt(GPIO_SUSPEND_L);

	/*
	 * Force the AP shutdown unless we are doing SYSJUMP. Otherwise,
	 * the AP could stay in strange state.
	 */
	if (!(system_get_reset_flags() & RESET_FLAG_SYSJUMP)) {
		CPRINTF("[%T not sysjump; forcing AP shutdown]\n");
		chipset_force_shutdown();
	}

	/* Leave power off only if requested by reset flags */
	if (!(system_get_reset_flags() & RESET_FLAG_AP_OFF)) {
		CPRINTF("[%T auto_power_on is set due to reset_flag 0x%x]\n",
			system_get_reset_flags());
		auto_power_on = 1;
	}

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
	/*
	 * TODO(crosbug.com/p/23822): Implement, if/when we take the AP down to
	 * a hard-off state.
	 */
}

void chipset_reset(int is_cold)
{
	/*
	 * TODO(crosbug.com/p/23822): Implement cold reset.  For now, all
	 * resets are warm resets.
	 */
	CPRINTF("[%T EC triggered warm reboot]\n");

	/*
	 * This is a hack to do an AP warm reboot while still preserving RAM
	 * contents. This is useful for looking at kernel log message contents
	 * from previous boot in cases where the AP/OS is hard hung.
	 */
	power_request = POWER_REQ_ON;
	task_wake(TASK_ID_CHIPSET);
}

void chipset_force_shutdown(void)
{
	/* Release the power button, if it was asserted */
	set_pmic_pwron(0);

	/* Assert AP reset to shutdown immediately */
	set_pmic_therm(1);
	udelay(PMIC_THERM_HOLD_TIME);
	set_pmic_therm(0);
}

/*****************************************************************************/

/**
 * Check if there has been a power-on event
 *
 * This checks all power-on event signals and returns non-zero if any have been
 * triggered (with debounce taken into account).
 *
 * @return non-zero if there has been a power-on event, 0 if not.
 */
static int check_for_power_on_event(void)
{
	/* check if system is already ON */
	if (gpio_get_level(GPIO_SOC1V8_XPSHOLD)) {
		CPRINTF("[%T system is on, thus clear auto_power_on]\n");
		auto_power_on = 0;  /* no need to arrange another power on */
		return 1;
	}

	/* power on requested at EC startup for recovery */
	if (auto_power_on) {
		auto_power_on = 0;
		return 2;
	}

	/* Check lid open */
	if (lid_opened) {
		lid_opened = 0;
		return 3;
	}

	/* check for power button press */
	if (power_button_is_pressed()) {
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
	/* Make sure we de-assert the PMI_THERM_L pin. */
	set_pmic_therm(0);

	/* Push the power button */
	set_pmic_pwron(1);
	usleep(PMIC_PWRON_DEBOUNCE_TIME);

	/* Initialize non-AP components if the AP is off. */
	if (!ap_on)
		hook_notify(HOOK_CHIPSET_PRE_INIT);

	ap_on = 1;
	disable_sleep(SLEEP_MASK_AP_RUN);
	powerled_set_state(POWERLED_STATE_ON);

	/* Call hooks now that AP is running */
	hook_notify(HOOK_CHIPSET_STARTUP);

	CPRINTF("[%T AP running ...]\n");
	return 0;
}

/**
 * Wait for the power button to be released
 *
 * @param timeout_us Timeout in microseconds, or -1 to wait forever
 * @return EC_SUCCESS if ok, or
 *         EC_ERROR_TIMEOUT if power button failed to release
 */
static int wait_for_power_button_release(unsigned int timeout_us)
{
	timestamp_t deadline;
	timestamp_t now = get_time();

	deadline.val = now.val + timeout_us;

	while (power_button_is_pressed()) {
		now = get_time();
		if (timeout_us < 0) {
			task_wait_event(-1);
		} else if (timestamp_expired(deadline, &now) ||
			(task_wait_event(deadline.val - now.val) ==
			TASK_EVENT_TIMER)) {
			CPRINTF("[%T power button not released in time]\n");
			return EC_ERROR_TIMEOUT;
		}
	}

	CPRINTF("[%T power button released]\n");
	return EC_SUCCESS;
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

#ifdef BOARD_nyan
	/*
	 * nyan's GPIO_SOC1V8_XPSHOLD will go low for about 20ms after initial
	 * high. Wait XPSHOLD_DEBOUNCE time, then check the signal again.
	 */
	udelay(XPSHOLD_DEBOUNCE);
#endif

	if (gpio_get_level(GPIO_SOC1V8_XPSHOLD) == 0) {
		CPRINTF("[%T XPSHOLD not seen in time]\n");
		return -1;
	}

	CPRINTF("[%T XPSHOLD seen]\n");
	return 0;
}

/**
 * Power off the AP
 */
static void power_off(void)
{
	/* Call hooks before we drop power rails */
	hook_notify(HOOK_CHIPSET_SHUTDOWN);
	/* switch off all rails */
	chipset_force_shutdown();
	ap_on = 0;
	ap_suspended = 0;
	lid_opened = 0;
	enable_sleep(SLEEP_MASK_AP_RUN);
	powerled_set_state(POWERLED_STATE_OFF);
	CPRINTF("[%T power shutdown complete]\n");
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
static int wait_for_power_on(void)
{
	int value;
	while (1) {
		value = check_for_power_on_event();
		if (!value) {
			task_wait_event(-1);
			continue;
		}

#ifdef HAS_TASK_CHARGER
		/*
		 * If the system is already on (value == 1), the kernel
		 * would handle low power condition and we should not
		 * shutdown the system from EC.
		 */
		if (value != 1 && charge_keep_power_off()) {
			CPRINTF("[%T power on ignored due to low battery]\n");
			continue;
		}
#endif

		CPRINTF("[%T power on %d]\n", value);
		return value;
	}
}

void chipset_task(void)
{
	int value;

	tegra_power_init();
	ap_on = 0;

	while (1) {
		/* Wait until we need to power on, then power on */
		wait_for_power_on();

		if (!power_on()) {
			int continue_power = 0;

			if (!react_to_xpshold(DELAY_RELEASE_PWRON)) {
				/* AP looks good */
				if (!wait_for_power_button_release(
					DELAY_SHUTDOWN_ON_POWER_HOLD))
					continue_power = 1;
			}
			set_pmic_pwron(0);
			if (continue_power) {
				power_button_was_pressed = 0;
				while (!(value = check_for_power_off_event()))
					task_wait_event(next_pwr_event());
				CPRINTF("[%T power ending loop %d]\n", value);
			}
		}
		power_off();
		wait_for_power_button_release(-1);
	}
}

static void powerbtn_tegra_changed(void)
{
	task_wake(TASK_ID_CHIPSET);
}
DECLARE_HOOK(HOOK_POWER_BUTTON_CHANGE, powerbtn_tegra_changed,
		HOOK_PRIO_DEFAULT);

/*****************************************************************************/
/* Console debug command */

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
	int v;

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

	if (!parse_bool(argv[1], &v))
		return EC_ERROR_PARAM1;

	power_request = v ? POWER_REQ_ON : POWER_REQ_OFF;
	ccprintf("Requesting power %s\n", power_req_name[power_request]);
	task_wake(TASK_ID_CHIPSET);

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(power, command_power,
			"on/off",
			"Turn AP power on/off",
			NULL);
