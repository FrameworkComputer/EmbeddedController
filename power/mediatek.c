/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * MEDIATEK SoC power sequencing module for Chrome EC
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
 *  - Holding pwron for 11s powers off the AP
 *  - Pressing and releasing pwron within that 11s is ignored
 *  - If POWER_GOOD is dropped by the AP, then we power the AP off
 *  - If SUSPEND_L goes low, enter suspend mode.
 *
 */

#include "battery.h"
#include "chipset.h" /* ./common/chipset.c implements chipset functions too */
#include "common.h"
#include "gpio.h"
#include "hooks.h"
#include "keyboard_scan.h"
#include "lid_switch.h"
#include "power.h"
#include "power_button.h"
#include "power_led.h"
#include "system.h"
#include "task.h"
#include "test_util.h"

#define CPRINTS(format, args...) cprints(CC_CHIPSET, format, ## args)

#define INT_BOTH_PULL_UP	(GPIO_INPUT | GPIO_PULL_UP | GPIO_INT_BOTH)

/* masks for power signals */
#define IN_POWER_GOOD POWER_SIGNAL_MASK(MTK_POWER_GOOD)
#define IN_SUSPEND POWER_SIGNAL_MASK(MTK_SUSPEND_ASSERTED)

/* Long power key press to force shutdown */
#define DELAY_FORCE_SHUTDOWN  (11000 * MSEC)	/* 11 seconds */

/*
 * The minimum time to assert the PMIC PWRON pin is 20ms.
 * Give it longer to ensure the PMIC doesn't lose it.
 */
#define PMIC_PWRON_DEBOUNCE_TIME  (60 * MSEC)

/*
 * The time to bootup the PMIC from power-off to power-on.
 */
#define PMIC_PWRON_PRESS_TIME   (3000 * MSEC)

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
 *    press or POWER_GOOD == 0).
 */
#define DELAY_SHUTDOWN_ON_POWER_HOLD	(11000 * MSEC)	/* 11 seconds */

/*
 * The hold time for pulling down the PMIC_WARM_RESET_L pin so that
 * the AP can entery the recovery mode (flash SPI flash from USB).
 */
#define PMIC_WARM_RESET_L_HOLD_TIME (4 * MSEC)

/*
 * The first time the PMIC sees power (AC or battery) it needs 200ms (+/-12%
 * oscillator tolerance) for the RTC startup. In addition there is a startup
 * time of approx. 0.5msec until V2_5 regulator starts up. */
#define PMIC_RTC_STARTUP (225 * MSEC)

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

/**
 * Return values for check_for_power_off_event().
 */
enum power_off_event_t {
	POWER_OFF_CANCEL,
	POWER_OFF_BY_POWER_BUTTON_PRESSED,
	POWER_OFF_BY_LONG_PRESS,
	POWER_OFF_BY_POWER_GOOD_LOST,
	POWER_OFF_BY_POWER_REQ,

	POWER_OFF_EVENT_COUNT,
};

/**
 * Return values for check_for_power_on_event().
 */
enum power_on_event_t {
	POWER_ON_CANCEL,
	POWER_ON_BY_IN_POWER_GOOD,
	POWER_ON_BY_AUTO_POWER_ON,
	POWER_ON_BY_LID_OPEN,
	POWER_ON_BY_POWER_BUTTON_PRESSED,
	POWER_ON_BY_POWER_REQ_NONE,

	POWER_ON_EVENT_COUNT,
};

/**
 * Parameters of mtk_backlight_override().
 */
enum blacklight_override_t {
	MTK_BACKLIGHT_FORCE_OFF,
	MTK_BACKLIGHT_CONTROL_BY_SOC,

	MTK_BACKLIGHT_OVERRIDE_COUNT,
};

/* Forward declaration */
static void chipset_turn_off_power_rails(void);

/**
 * Set the AP RESET signal.
 *
 * This function is for backward-compatible.
 *
 * AP_RESET_H (PB3) is stuffed before rev <= 2.0 and connected to PMIC RESET.
 * After rev >= 2.2, this is removed. This should not effected the new board.
 *
 * @param asserted  Assert (=1) or deassert (=0) the signal.  This is the
 *                  logical level of the pin, not the physical level.
 */
static void set_ap_reset(int asserted)
{
	/* Signal is active-high */
	CPRINTS("set_ap_reset(%d)", asserted);
	gpio_set_level(GPIO_AP_RESET_H, asserted);
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
	/* Signal is active-high */
	CPRINTS("set_pmic_pwron(%d)", asserted);
	gpio_set_level(GPIO_PMIC_PWRON_H, asserted);
}

/**
 * Set the PMIC WARM RESET signal.
 *
 * @param asserted	Resetting (=0) or idle (=1)
 */
static void set_pmic_warm_reset(int asserted)
{
	/* Signal is active-high */
	/* @param asserted: Resetting (=0) or idle (=1) */
	gpio_set_level(GPIO_PMIC_WARM_RESET_H, asserted);
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

	/*
	 * Check for power button press.
	 */
	if (power_button_is_pressed()) {
		pressed = POWER_OFF_BY_POWER_BUTTON_PRESSED;
	} else if (power_request == POWER_REQ_OFF) {
		power_request = POWER_REQ_NONE;
		/* return non-zero for shudown down */
		return POWER_OFF_BY_POWER_REQ;
	}

#ifdef HAS_TASK_KEYSCAN
	/* Dis/Enable keyboard scanning when the power button state changes */
	if (!pressed || pressed != power_button_was_pressed)
		keyboard_scan_enable(!pressed, KB_SCAN_DISABLE_POWER_BUTTON);
#endif

	now = get_time();
	if (pressed) {
#ifndef CONFIG_PMIC_FW_LONG_PRESS_TIMER
		/*
		 * Only assert PMIC_PWRON if PMIC supports long-press
		 * power off.
		 */
		CPRINTS("PMIC long-press power off\n");
		set_pmic_pwron(1);
		usleep(PMIC_PWRON_DEBOUNCE_TIME);
#endif

		if (!power_button_was_pressed) {
			power_off_deadline.val = now.val + DELAY_FORCE_SHUTDOWN;
			CPRINTS("power waiting for long press %u",
				power_off_deadline.le.lo);
#ifdef CONFIG_PMIC_FW_LONG_PRESS_TIMER
			/* Ensure we will wake up to check the power key */
			timer_arm(power_off_deadline, TASK_ID_CHIPSET);
#endif
		} else if (timestamp_expired(power_off_deadline, &now)) {
			power_off_deadline.val = 0;
			CPRINTS("power off after long press now=%u, %u",
				now.le.lo, power_off_deadline.le.lo);
			return POWER_OFF_BY_LONG_PRESS;
		}
	} else if (power_button_was_pressed) {
		CPRINTS("power off cancel");
		set_pmic_pwron(0);
#ifdef CONFIG_PMIC_FW_LONG_PRESS_TIMER
		timer_cancel(TASK_ID_CHIPSET);
#endif
	}

	power_button_was_pressed = pressed;

	/* POWER_GOOD released by AP : shutdown immediately */
	if (!power_has_signals(IN_POWER_GOOD))
		return POWER_OFF_BY_POWER_GOOD_LOST;

	return POWER_OFF_CANCEL;
}

/**
 * Set the LCD backlight enable pin and override the signal from SoC.
 *
 * @param asserted	MTK_BACKLIGHT_FORCE_OFF, force off the panel backlight
 *			MTK_BACKLIGHT_CONTROL_BY_SOC, leave the control to SOC
 */
static void mtk_backlight_override(enum blacklight_override_t asserted)
{
	/* Signal is active-low */
	gpio_set_level(GPIO_EC_BL_OVERRIDE, !asserted);
}

static void mtk_lid_event(void)
{
	enum blacklight_override_t bl_override;

	/* Override the panel backlight enable signal from SoC,
	 * force the backlight off on lid close.
	 */
	bl_override = lid_is_open() ?
		MTK_BACKLIGHT_CONTROL_BY_SOC :
		MTK_BACKLIGHT_FORCE_OFF;
	mtk_backlight_override(bl_override);

	/* Power task only cares about lid-open events */
	if (!lid_is_open())
		return;

	lid_opened = 1;
	task_wake(TASK_ID_CHIPSET);
}
DECLARE_HOOK(HOOK_LID_CHANGE, mtk_lid_event, HOOK_PRIO_DEFAULT);

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
		 * The warm reset triggers AP into the recovery mode (
		 * flash SPI from USB).
		 */
		chipset_reset(0);

		init_power_state = POWER_G3;
	} else {
		/* In the SYSJUMP case, we check if the AP is on */
		if (power_get_signals() & IN_POWER_GOOD) {
			CPRINTS("SOC ON\n");
			init_power_state = POWER_S0;
			disable_sleep(SLEEP_MASK_AP_RUN);
		} else {
			CPRINTS("SOC OFF\n");
			init_power_state = POWER_G3;
			enable_sleep(SLEEP_MASK_AP_RUN);
		}
	}

	/* Leave power off only if requested by reset flags */
	if (!(reset_flags & RESET_FLAG_AP_OFF) &&
	    !(reset_flags & RESET_FLAG_SYSJUMP)) {
		CPRINTS("reset_flag 0x%x", reset_flags);
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
	/* set_pmic_source(0); */

	usleep(PMIC_THERM_HOLD_TIME);

	/* Keep AP and PMIC in reset the whole time */
	set_pmic_warm_reset(1);

	/* Hold the reset pin so that the AP stays in off mode (rev <= 2.0) */
	set_ap_reset(1);
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
			CPRINTS("system is on, but RESET_FLAG_AP_OFF is on");
			return POWER_ON_CANCEL;
		} else {
			CPRINTS("system is on, thus clear " "auto_power_on");
			/* no need to arrange another power on */
			auto_power_on = 0;
			return POWER_ON_BY_IN_POWER_GOOD;
		}
	} else {
		CPRINTS("POWER_GOOD is not asserted");
	}

	/* power on requested at EC startup for recovery */
	if (auto_power_on) {
		auto_power_on = 0;
		return POWER_ON_BY_AUTO_POWER_ON;
	}

	/* Check lid open */
	if (lid_opened) {
		lid_opened = 0;
		return POWER_ON_BY_LID_OPEN;
	}

	/* check for power button press */
	if (power_button_is_pressed())
		return POWER_ON_BY_POWER_BUTTON_PRESSED;

	if (power_request == POWER_REQ_ON) {
		power_request = POWER_REQ_NONE;
		return POWER_ON_BY_POWER_REQ_NONE;
	}

	return POWER_OFF_CANCEL;
}

/**
 * Power on the AP
 */
static void power_on(void)
{
	uint64_t t;

	CPRINTS("power_on AP");

	/* Set pull-up and enable interrupt */
	gpio_set_flags(power_signal_list[MTK_SUSPEND_ASSERTED].gpio,
		       GPIO_INPUT | GPIO_PULL_UP | GPIO_INT_BOTH);

	/* Make sure we de-assert and AP_RESET_L pin. */
	set_ap_reset(0);

	/*
	 * Before we push PMIC power button, wait for the PMI RTC ready, which
	 * takes PMIC_RTC_STARTUP from the AC/battery is plugged in.
	 */
	t = get_time().val;
	if (t < PMIC_RTC_STARTUP) {
		uint32_t wait = PMIC_RTC_STARTUP - t;

		CPRINTS("wait for %dms for PMIC RTC start-up", wait / MSEC);
		usleep(wait);
	}

	/*
	 * When power_on() is called, we are at S5S3. Initialize components
	 * to ready state before AP is up.
	 */
	hook_notify(HOOK_CHIPSET_PRE_INIT);

	/* Push the power button */
	set_pmic_pwron(1);
	usleep(PMIC_PWRON_PRESS_TIME);

	/* Wait till the AP has SPI ready */
	/* usleep(PMIC_SPI_READY_TIME); */

	/* enable interrupt */
	gpio_set_flags(GPIO_SUSPEND_L, INT_BOTH_PULL_UP);
	gpio_set_flags(GPIO_EC_INT, GPIO_OUTPUT | GPIO_OUT_HIGH);

	disable_sleep(SLEEP_MASK_AP_RUN);
#ifdef HAS_TASK_POWERLED
	powerled_set_state(POWERLED_STATE_ON);
#endif
	/* Call hooks now that AP is running */
	hook_notify(HOOK_CHIPSET_STARTUP);

	CPRINTS("AP running ...");
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
			CPRINTS("power button not released in time");
			return EC_ERROR_TIMEOUT;
		}
	}

	CPRINTS("power button released");
	power_button_was_pressed = 0;
	return EC_SUCCESS;
}

/**
 * Power off the AP
 */
static void power_off(void)
{
	/* Call hooks before we drop power rails */
	hook_notify(HOOK_CHIPSET_SHUTDOWN);
	/* switch off all rails */
	chipset_turn_off_power_rails();

	/* Change SUSPEND_L pin to high-Z to reduce power draw. */
	gpio_set_flags(power_signal_list[MTK_SUSPEND_ASSERTED].gpio,
		       GPIO_INPUT);

	lid_opened = 0;
	enable_sleep(SLEEP_MASK_AP_RUN);
#ifdef HAS_TASK_POWERLED
	powerled_set_state(POWERLED_STATE_OFF);
#endif
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
		CPRINTS("assert GPIO_PMIC_WARM_RESET_H for %d ms",
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
		power_on();
		if (power_wait_signals(IN_POWER_GOOD) == EC_SUCCESS) {
			CPRINTS("POWER_GOOD seen");
			if (wait_for_power_button_release
			    (DELAY_SHUTDOWN_ON_POWER_HOLD) == EC_SUCCESS) {
				set_pmic_pwron(0);
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
		set_pmic_pwron(0);
		return POWER_S5;

	case POWER_S3:
		if (!(power_get_signals() & IN_POWER_GOOD))
			return POWER_S3S5;
		else if (!(power_get_signals() & IN_SUSPEND))
			return POWER_S3S0;
		return state;

	case POWER_S3S0:
#ifdef HAS_TASK_POWERLED
		powerled_set_state(POWERLED_STATE_ON);
#endif
		hook_notify(HOOK_CHIPSET_RESUME);
		return POWER_S0;

	case POWER_S0:
		value = check_for_power_off_event();
		if (value) {
			CPRINTS("power off %d", value);
			power_off();
			return POWER_S0S3;
		} else if (power_get_signals() & IN_SUSPEND) {
			return POWER_S0S3;
		}
		return state;

	case POWER_S0S3:
#ifdef HAS_TASK_POWERLED
		if (lid_is_open())
			powerled_set_state(POWERLED_STATE_SUSPEND);
		else
			powerled_set_state(POWERLED_STATE_OFF);
#endif
		/* Call hooks here since we don't know it prior to AP suspend */
		hook_notify(HOOK_CHIPSET_SUSPEND);
		return POWER_S3;

	case POWER_S3S5:
		wait_for_power_button_release(-1);
		return POWER_S5;

	case POWER_S5G3:
		return POWER_G3;
	}

	return state;
}

static void powerbtn_mtk_changed(void)
{
	task_wake(TASK_ID_CHIPSET);
}

DECLARE_HOOK(HOOK_POWER_BUTTON_CHANGE, powerbtn_mtk_changed, HOOK_PRIO_DEFAULT);
