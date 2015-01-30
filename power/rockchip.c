/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * Rockchip SoC power sequencing module for Chrome EC
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
 *  - Holding pwron for 10.2s powers off the AP
 *  - Pressing and releasing pwron within that 10.2s is ignored
 *  - If POWER_GOOD is dropped by the pmic, then we cut off the pmic source
 *  - If SUSPEND_L goes low, enter suspend mode.
 *
 */

#include "battery.h"
#include "charge_state.h"
#include "chipset.h"  /* This module implements chipset functions too */
#include "clock.h"
#include "common.h"
#include "console.h"
#include "gpio.h"
#include "hooks.h"
#include "lid_switch.h"
#include "keyboard_scan.h"
#include "power.h"
#include "power_button.h"
#include "power_led.h"
#include "system.h"
#include "task.h"
#include "timer.h"
#include "util.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_CHIPSET, outstr)
#define CPRINTS(format, args...) cprints(CC_CHIPSET, format, ## args)

/* masks for power signals */
#define IN_POWER_GOOD POWER_SIGNAL_MASK(RK_POWER_GOOD)
#define IN_SUSPEND POWER_SIGNAL_MASK(RK_SUSPEND_ASSERTED)

/* Long power key press to force shutdown */
#define DELAY_FORCE_SHUTDOWN  (8 * SECOND)

/*
 * If the power key is pressed to turn on, then held for this long, we
 * power off.
 *
 * Normal case: User releases power button and chipset_task() goes
 *    into the inner loop, waiting for next event to occur (power button
 *    press or power good == 0).
 */
#define DELAY_SHUTDOWN_ON_POWER_HOLD	(8 * SECOND)

/*
 * The hold time for pulling down the PMIC_WARM_RESET_L pin so that
 * the AP can entery the recovery mode (flash SPI flash from USB).
 */
#define PMIC_WARM_RESET_L_HOLD_TIME (4 * MSEC)

/*
 * Startup time for the PMIC source regulator.
 */
#define PMIC_SOURCE_STARTUP_TIME (50 * MSEC)

/*
 * Time before PMIC can be reset.
 */
#define PMIC_STARTUP_MS 300

/* TODO(crosbug.com/p/25047): move to HOOK_POWER_BUTTON_CHANGE */
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


/* Forward declaration */
static void chipset_turn_off_power_rails(void);


/**
 * Set the PMIC WARM RESET signal.
 *
 * @param asserted	Resetting (=0) or idle (=1)
 */
static void set_pmic_warm_reset(int asserted)
{
	/* Signal is active-low */
	gpio_set_level(GPIO_PMIC_WARM_RESET_L, asserted ? 0 : 1);
}


/**
 * Set the PMIC PWRON signal.
 *
 * @param asserted	Assert (=1) or deassert (=0) the signal.
 */
static void set_pmic_pwron(int asserted)
{
	/* Signal is active-high */
	gpio_set_level(GPIO_PMIC_PWRON, asserted ? 1 : 0);
}

/**
 * Set the PMIC source to force shutdown the AP.
 *
 * @param asserted	Assert (=1) or deassert (=0) the signal.
 */
static void set_pmic_source(int asserted)
{
	/* Signal is active-high */
	gpio_set_level(GPIO_PMIC_SOURCE_PWREN, asserted ? 1 : 0);
}

/**
 * Check for some event triggering the shutdown.
 *
 * It can be either a long power button press or a shutdown triggered from the
 * AP and detected by reading POWER_GOOD.
 *
 * @return non-zero if a shutdown should happen, 0 if not
 */
static int check_for_power_off_event(void)
{
	timestamp_t now;
	int pressed = 0;
	int ret = 0;

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
		keyboard_scan_enable(!pressed, KB_SCAN_DISABLE_POWER_BUTTON);
#endif

	now = get_time();
	if (pressed) {
		if (!power_button_was_pressed) {
			power_off_deadline.val = now.val + DELAY_FORCE_SHUTDOWN;
			CPRINTS("power waiting for long press %u",
				power_off_deadline.le.lo);
			/* Ensure we will wake up to check the power key */
			timer_arm(power_off_deadline, TASK_ID_CHIPSET);
		} else if (timestamp_expired(power_off_deadline, &now)) {
			power_off_deadline.val = 0;
			CPRINTS("power off after long press now=%u, %u",
				now.le.lo, power_off_deadline.le.lo);
			return 2;
		}
	} else if (power_button_was_pressed) {
		CPRINTS("power off cancel");
		timer_cancel(TASK_ID_CHIPSET);
	}

	/* POWER_GOOD released by AP : shutdown immediately */
	if (!power_has_signals(IN_POWER_GOOD)) {
		if (power_button_was_pressed)
			timer_cancel(TASK_ID_CHIPSET);
		ret = 3;
	}

	power_button_was_pressed = pressed;

	return ret;
}

static void rockchip_lid_event(void)
{
	/* Power task only cares about lid-open events */
	if (!lid_is_open())
		return;

	lid_opened = 1;
	task_wake(TASK_ID_CHIPSET);
}
DECLARE_HOOK(HOOK_LID_CHANGE, rockchip_lid_event, HOOK_PRIO_DEFAULT);

enum power_state power_chipset_init(void)
{
	int init_power_state;
	uint32_t reset_flags = system_get_reset_flags();

	/*
	 * Force the AP shutdown unless we are doing SYSJUMP. Otherwise,
	 * the AP could stay in strange state.
	 */
	if (!(reset_flags & RESET_FLAG_SYSJUMP)) {
		CPRINTS("not sysjump; forcing AP shutdown");
		chipset_turn_off_power_rails();

		/*
		 * The warm reset triggers AP into the RK recovery mode (
		 * flash SPI from USB).
		 */
		chipset_reset(0);

		init_power_state = POWER_G3;
	} else {
		/* In the SYSJUMP case, we check if the AP is on */
		if (power_get_signals() & IN_POWER_GOOD)
			init_power_state = POWER_S0;
		else
			init_power_state = POWER_G3;
	}

	/* Leave power off only if requested by reset flags */
	if (!(reset_flags & RESET_FLAG_AP_OFF) &&
	    !(reset_flags & RESET_FLAG_SYSJUMP)) {
		CPRINTS("auto_power_on set due to reset_flag 0x%x",
			system_get_reset_flags());
		auto_power_on = 1;
	}

	/*
	 * Some batteries use clock stretching feature, which requires
	 * more time to be stable. See http://crosbug.com/p/28289
	 */
	battery_wait_for_stable();

	return init_power_state;
}

/*****************************************************************************/
/* Chipset interface */

static void chipset_turn_off_power_rails(void)
{
	/* Release the power on pin, if it was asserted */
	set_pmic_pwron(0);
	/* Close the pmic power source immediately */
	set_pmic_source(0);

	/* Keep AP and PMIC in reset the whole time */
	set_pmic_warm_reset(1);
}

void chipset_force_shutdown(void)
{
	chipset_turn_off_power_rails();

	/* clean-up internal variable */
	power_request = POWER_REQ_NONE;
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
	int ap_off_flag;

	ap_off_flag = system_get_reset_flags() & RESET_FLAG_AP_OFF;
	system_clear_reset_flags(RESET_FLAG_AP_OFF);
	/* check if system is already ON */
	if (power_get_signals() & IN_POWER_GOOD) {
		if (ap_off_flag) {
			CPRINTS(
				"system is on, but "
				"RESET_FLAG_AP_OFF is on");
			return 0;
		} else {
			CPRINTS(
				"system is on, thus clear "
				"auto_power_on");
			/* no need to arrange another power on */
			auto_power_on = 0;
			return 1;
		}
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
	if (power_button_is_pressed())
		return 4;

	if (power_request == POWER_REQ_ON) {
		power_request = POWER_REQ_NONE;
		return 5;
	}

	return 0;
}

/**
 * Power on the AP
 */
static void power_on(void)
{
	int i;

	set_pmic_source(1);
	usleep(PMIC_SOURCE_STARTUP_TIME);

	set_pmic_pwron(1);
	/*
	 * BUG Workaround(crosbug.com/p/31635): usleep hangs in task when using
	 * big delays.
	 */
	for (i = 0; i < PMIC_STARTUP_MS; i++)
		usleep(1 * MSEC);

	set_pmic_warm_reset(0);
}

/**
 * Power off the AP
 */
static void power_off(void)
{
	unsigned int power_off_timeout = 100; /* ms */

	/* Call hooks before we drop power rails */
	hook_notify(HOOK_CHIPSET_SHUTDOWN);
	/* switch off all rails */
	chipset_turn_off_power_rails();
	/* Change SUSPEND_L and EC_INT pin to high-Z to reduce power draw. */
	gpio_set_flags(GPIO_SUSPEND_L, GPIO_INPUT);
	gpio_set_flags(GPIO_EC_INT, GPIO_INPUT);

	/* Wait till we actually turn off to not mess up the state machine. */
	while (power_get_signals() & IN_POWER_GOOD) {
		msleep(1);
		power_off_timeout--;
		ASSERT(power_off_timeout);
	}

	lid_opened = 0;
	enable_sleep(SLEEP_MASK_AP_RUN);
	powerled_set_state(POWERLED_STATE_OFF);

	CPRINTS("power shutdown complete");
}

void chipset_reset(int is_cold)
{
	if (is_cold) {
		CPRINTS("EC triggered cold reboot");
		power_off();
		/* After POWER_GOOD is dropped off,
		 * the system will be on again
		 */
		power_request = POWER_REQ_ON;
	} else {
		CPRINTS("EC triggered warm reboot");
		CPRINTS("assert GPIO_PMIC_WARM_RESET_L for %d ms",
				PMIC_WARM_RESET_L_HOLD_TIME / MSEC);
		set_pmic_warm_reset(1);
		usleep(PMIC_WARM_RESET_L_HOLD_TIME);
		set_pmic_warm_reset(0);
	}
}

enum power_state power_handle_state(enum power_state state)
{
	int value;
	static int boot_from_g3;

	switch (state) {
	case POWER_G3:
		boot_from_g3 = check_for_power_on_event();
		if (boot_from_g3)
			return POWER_G3S5;
		break;

	case POWER_G3S5:
		return POWER_S5;

	case POWER_S5:
		if (boot_from_g3) {
			value = boot_from_g3;
			boot_from_g3 = 0;
		} else {
			value = check_for_power_on_event();
		}

		if (value) {
			CPRINTS("power on %d", value);
			return POWER_S5S3;
		}
		return state;

	case POWER_S5S3:
		hook_notify(HOOK_CHIPSET_PRE_INIT);

		power_on();

		disable_sleep(SLEEP_MASK_AP_RUN);
		powerled_set_state(POWERLED_STATE_ON);

		if (power_wait_signals(IN_POWER_GOOD) == EC_SUCCESS) {
			CPRINTS("POWER_GOOD seen");
			if (power_button_wait_for_release(
					DELAY_SHUTDOWN_ON_POWER_HOLD) ==
					EC_SUCCESS) {
				power_button_was_pressed = 0;
				set_pmic_pwron(0);

				/* setup misc gpio for S3/S0 functionality */
				gpio_set_flags(GPIO_SUSPEND_L, GPIO_INPUT
					| GPIO_INT_BOTH | GPIO_PULL_DOWN);
				gpio_set_flags(GPIO_EC_INT, GPIO_OUTPUT
						| GPIO_OUT_HIGH);

				/* Call hooks now that AP is running */
				hook_notify(HOOK_CHIPSET_STARTUP);

				return POWER_S3;
			} else {
				CPRINTS("long-press button, shutdown");
				power_off();
				/*
				 * Since the AP may be up already, return S0S3
				 * state to go through the suspend hook.
				 */
				return POWER_S0S3;
			}
		} else {
			CPRINTS("POWER_GOOD not seen in time");
		}

		chipset_turn_off_power_rails();
		return POWER_S5;

	case POWER_S3:
		if (!(power_get_signals() & IN_POWER_GOOD))
			return POWER_S3S5;
		else if (!(power_get_signals() & IN_SUSPEND))
			return POWER_S3S0;
		return state;

	case POWER_S3S0:
		powerled_set_state(POWERLED_STATE_ON);
		hook_notify(HOOK_CHIPSET_RESUME);
		return POWER_S0;

	case POWER_S0:
		value = check_for_power_off_event();
		if (value) {
			CPRINTS("power off %d", value);
			power_off();
			return POWER_S0S3;
		} else if (power_get_signals() & IN_SUSPEND)
			return POWER_S0S3;
		return state;

	case POWER_S0S3:
		if (lid_is_open())
			powerled_set_state(POWERLED_STATE_SUSPEND);
		else
			powerled_set_state(POWERLED_STATE_OFF);
		/* Call hooks here since we don't know it prior to AP suspend */
		hook_notify(HOOK_CHIPSET_SUSPEND);
		return POWER_S3;

	case POWER_S3S5:
		power_button_wait_for_release(-1);
		power_button_was_pressed = 0;
		return POWER_S5;

	case POWER_S5G3:
		return POWER_G3;
	}

	return state;
}

static void powerbtn_rockchip_changed(void)
{
	task_wake(TASK_ID_CHIPSET);
}
DECLARE_HOOK(HOOK_POWER_BUTTON_CHANGE, powerbtn_rockchip_changed,
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
