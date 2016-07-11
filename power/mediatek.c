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
 *  - Hold pwron turns on the AP, and then 8s later turns it off and leaves
 *    it off until pwron is released and pressed again
 *
 *  When powered on:
 *  - The PMIC PWRON signal is released <= 1 second after the power button is
 *    released
 *  - Holding pwron for 8s powers off the AP
 *  - Pressing and releasing pwron within that 8s is ignored
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
#include "util.h"

#define CPRINTS(format, args...) cprints(CC_CHIPSET, format, ## args)

#define INT_BOTH_PULL_UP	(GPIO_INPUT | GPIO_PULL_UP | GPIO_INT_BOTH)

/* masks for power signals */
#define IN_POWER_GOOD POWER_SIGNAL_MASK(MTK_POWER_GOOD)
#define IN_SUSPEND POWER_SIGNAL_MASK(MTK_SUSPEND_ASSERTED)

/* Long power key press to force shutdown */
#define DELAY_FORCE_SHUTDOWN  (8000 * MSEC)	/* 8 seconds */

/*
 * The power signal from SoC should be kept at least 50ms.
 */
#define POWER_DEBOUNCE_TIME     (50 * MSEC)

/*
 * The suspend signal from SoC should be kept at least 50ms.
 */
#define SUSPEND_DEBOUNCE_TIME   (50 * MSEC)

/*
 * The time to bootup the PMIC from power-off to power-on.
 */
#define PMIC_PWRON_PRESS_TIME   (5000 * MSEC)

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
#define DELAY_SHUTDOWN_ON_POWER_HOLD	(8000 * MSEC)	/* 8 seconds */

/*
 * The hold time for pulling down the PMIC_WARM_RESET_H pin so that
 * the AP can entery the recovery mode (flash SPI flash from USB).
 */
#define PMIC_WARM_RESET_H_HOLD_TIME (4 * MSEC)

/*
 * The hold time for pulling down the SYSTEM_POWER_H pin.
 */
#define PMIC_COLD_RESET_L_HOLD_TIME \
	(SUSPEND_DEBOUNCE_TIME + POWER_DEBOUNCE_TIME + (20 * MSEC))

/*
 * The first time the PMIC sees power (AC or battery) it needs 200ms (+/-12%
 * oscillator tolerance) for the RTC startup. In addition there is a startup
 * time of approx. 0.5msec until V2_5 regulator starts up. */
#define PMIC_RTC_STARTUP (225 * MSEC)

/* Wait for 5V power source stable */
#define PMIC_WAIT_FOR_5V_POWER_GOOD (1 * MSEC)

/*
 * If POWER_GOOD is lost, wait for PMIC to turn off its power completely
 * before we turn off VBAT by set_system_power(0)
 */
#define PMIC_POWER_OFF_DELAY (50 * MSEC)

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
 * Check the suspend signal is on after SUSPEND_DEBOUNCE_TIME to avoid transient
 * state.
 *
 * @return non-zero if SUSPEND is asserted.
 */
static int is_suspend_asserted(void)
{
#ifdef BOARD_OAK
	if ((power_get_signals() & IN_SUSPEND) &&
	    (system_get_board_version() < 4))
		usleep(SUSPEND_DEBOUNCE_TIME);
#endif

	return power_get_signals() & IN_SUSPEND;
}

/**
 * Check the suspend signal is off after SUSPEND_DEBOUNCE_TIME to avoid
 * transient state.
 *
 * @return non-zero if SUSPEND is deasserted.
 */
static int is_suspend_deasserted(void)
{
#ifdef BOARD_OAK
	if (!(power_get_signals() & IN_SUSPEND) &&
	    (system_get_board_version() < 4))
		usleep(SUSPEND_DEBOUNCE_TIME);
#endif

	return !(power_get_signals() & IN_SUSPEND);
}

/**
 * Check power good signal is on after POWER_DEBOUNCE_TIME to avoid transient
 * state.
 *
 * @return non-zero if POWER_GOOD is asserted.
 */
static int is_power_good_asserted(void)
{
	if (!gpio_get_level(GPIO_SYSTEM_POWER_H))
		return 0;
#ifdef BOARD_OAK
	else if ((power_get_signals() & IN_POWER_GOOD) &&
		 (system_get_board_version() < 4))
		usleep(POWER_DEBOUNCE_TIME);
#endif

	return power_get_signals() & IN_POWER_GOOD;
}

/**
 * Check power good signal is off after POWER_DEBOUNCE_TIME to avoid transient
 * state.
 *
 * @return non-zero if POWER_GOOD is deasserted.
 */
static int is_power_good_deasserted(void)
{
#ifdef BOARD_OAK
	/*
	 * Warm reset key from servo board lets the POWER_GOOD signal
	 * deasserted temporarily (about 1~2 seconds) on rev4.
	 * In order to detect this case, check the AP_RESET_L status,
	 * ignore the transient state if reset key is pressing.
	 */
	if (system_get_board_version() >= 4) {
		if (0 == gpio_get_level(GPIO_AP_RESET_L))
			return 0;
	} else {
		if (!(power_get_signals() & IN_POWER_GOOD))
			usleep(POWER_DEBOUNCE_TIME);
	}
#endif
	if (0 == gpio_get_level(GPIO_AP_RESET_L))
		return 0;

	return !(power_get_signals() & IN_POWER_GOOD);
}

/**
 * Set the system power signal.
 *
 * @param asserted	off (=0) or on (=1)
 */
static void set_system_power(int asserted)
{
	CPRINTS("set_system_power(%d)", asserted);
	gpio_set_level(GPIO_SYSTEM_POWER_H, asserted);
}

/**
 * Set the PMIC PWRON signal.
 *
 * Note that asserting requires holding for PMIC_PWRON_PRESS_TIME.
 *
 * @param asserted	Assert (=1) or deassert (=0) the signal.  This is the
 *			logical level of the pin, not the physical level.
 */
static void set_pmic_pwron(int asserted)
{
	timestamp_t poll_deadline;
	/* Signal is active-high */
	CPRINTS("set_pmic_pwron(%d)", asserted);
	/* Oak rev1 power-on sequence:
	 *   raise GPIO_SYSTEM_POWER_H
	 *   wait for 5V power good, timeout 1 second
	 */
	if (asserted) {
		set_system_power(asserted);
		poll_deadline = get_time();
		poll_deadline.val += SECOND;
		while (asserted && !gpio_get_level(GPIO_5V_POWER_GOOD) &&
		       get_time().val < poll_deadline.val)
			usleep(PMIC_WAIT_FOR_5V_POWER_GOOD);
		if (!gpio_get_level(GPIO_5V_POWER_GOOD))
			CPRINTS("5V power not ready");
	}

	gpio_set_level(GPIO_PMIC_PWRON_H, asserted);
}

/**
 * Set the WARM RESET signal.
 *
 * @param asserted	off (=0) or on (=1)
 */
static void set_warm_reset(int asserted)
{
	board_set_ap_reset(asserted);
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
			return POWER_OFF_BY_LONG_PRESS;
		}
	} else if (power_button_was_pressed) {
		CPRINTS("power off cancel");
		timer_cancel(TASK_ID_CHIPSET);
	}

	power_button_was_pressed = pressed;

	/* POWER_GOOD released by AP : shutdown immediate */
	if (is_power_good_deasserted()) {
		/*
		 * Cancel long press timer if power is lost and the power button
		 * still press, otherwise EC will crash.
		 */
		if (power_button_was_pressed)
			timer_cancel(TASK_ID_CHIPSET);

		CPRINTS("POWER_GOOD is lost");
		return POWER_OFF_BY_POWER_GOOD_LOST;
	}

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
		if (is_power_good_asserted()) {
			CPRINTS("SOC ON");
			/*
			 * Check and release PMIC power button signal,
			 * if it's deferred callback function is not triggered
			 * in RO before SYSJUMP.
			 */
			if (gpio_get_level(GPIO_PMIC_PWRON_H))
				set_pmic_pwron(0);

			init_power_state = POWER_S0;
			if (is_suspend_asserted())
				enable_sleep(SLEEP_MASK_AP_RUN);
			else
				disable_sleep(SLEEP_MASK_AP_RUN);
		} else {
			CPRINTS("SOC OFF");
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

	/* system power off */
	usleep(PMIC_POWER_OFF_DELAY);
	set_system_power(0);
}

void chipset_force_shutdown(void)
{
	chipset_turn_off_power_rails();

	/* clean-up internal variable */
	power_request = POWER_REQ_NONE;
}

/*****************************************************************************/

/**
 * Power off the AP
 */
static void power_off(void)
{
	/* Check the power off status */
	if (!gpio_get_level(GPIO_SYSTEM_POWER_H))
		return;

	/* Call hooks before we drop power rails */
	hook_notify(HOOK_CHIPSET_SHUTDOWN);
	/* switch off all rails */
	chipset_turn_off_power_rails();

	/* Change SUSPEND_L pin to high-Z to reduce power draw. */
	gpio_set_flags(power_signal_list[MTK_SUSPEND_ASSERTED].gpio,
		       GPIO_INPUT);

	/* Change EC_INT to low */
	gpio_set_level(GPIO_EC_INT_L, 0);

	lid_opened = 0;
	enable_sleep(SLEEP_MASK_AP_RUN);
#ifdef HAS_TASK_POWERLED
	powerled_set_state(POWERLED_STATE_OFF);
#endif
	CPRINTS("power shutdown complete");
}

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
	if (is_power_good_asserted()) {
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
		if (ap_off_flag) {
			CPRINTS("RESET_FLAG_AP_OFF is on");
			power_off();
			return POWER_ON_CANCEL;
		}

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

void release_pmic_pwron_deferred(void)
{
	/* Release PMIC power button */
	set_pmic_pwron(0);
}
DECLARE_DEFERRED(release_pmic_pwron_deferred);

/**
 * Power on the AP
 */
static void power_on(void)
{
	uint64_t t;

	/* Set pull-up and enable interrupt */
	gpio_set_flags(power_signal_list[MTK_SUSPEND_ASSERTED].gpio,
		       GPIO_INPUT | GPIO_PULL_UP | GPIO_INT_BOTH);

	/* Make sure we de-assert and GPIO_PMIC_WARM_RESET_H pin. */
	set_warm_reset(0);

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
	hook_call_deferred(&release_pmic_pwron_deferred_data,
			   PMIC_PWRON_PRESS_TIME);

	/* enable interrupt */
	gpio_set_flags(GPIO_SUSPEND_L, INT_BOTH_PULL_UP);

#ifdef BOARD_OAK
	if (system_get_board_version() <= 3)
		gpio_set_flags(GPIO_EC_INT_L, GPIO_OUTPUT | GPIO_OUT_HIGH);
	else
		gpio_set_flags(GPIO_EC_INT_L, GPIO_ODR_HIGH);
#else
	gpio_set_flags(GPIO_EC_INT_L, GPIO_ODR_HIGH);
#endif

	disable_sleep(SLEEP_MASK_AP_RUN);
#ifdef HAS_TASK_POWERLED
	powerled_set_state(POWERLED_STATE_ON);
#endif
	/* Call hooks now that AP is running */
	hook_notify(HOOK_CHIPSET_STARTUP);

	CPRINTS("AP running ...");
}

void chipset_reset(int is_cold)
{
	if (is_cold) {
		CPRINTS("EC triggered cold reboot");
		set_system_power(0);
		usleep(PMIC_COLD_RESET_L_HOLD_TIME);
		/* Press the PMIC power button */
		set_pmic_pwron(1);
		hook_call_deferred(&release_pmic_pwron_deferred_data,
				   PMIC_PWRON_PRESS_TIME);
	} else {
		CPRINTS("EC triggered warm reboot");
		set_warm_reset(1);
		usleep(PMIC_WARM_RESET_H_HOLD_TIME);
		/* deassert the reset signals */
		set_warm_reset(0);
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
			power_button_was_pressed = 0;
			return POWER_S3;
		} else {
			CPRINTS("POWER_GOOD not seen in time");
		}
		set_pmic_pwron(0);
		return POWER_S5;

	case POWER_S3:
		if (is_power_good_deasserted()) {
			power_off();
			return POWER_S3S5;
		} else if (is_suspend_deasserted())
			return POWER_S3S0;
		return state;

	case POWER_S3S0:
		disable_sleep(SLEEP_MASK_AP_RUN);
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
		} else if (is_suspend_asserted())
			return POWER_S0S3;
		return state;

	case POWER_S0S3:
#ifdef HAS_TASK_POWERLED
		if (lid_is_open())
			powerled_set_state(POWERLED_STATE_SUSPEND);
		else
			powerled_set_state(POWERLED_STATE_OFF);
#endif
		/*
		 * if the power button is pressing, we need cancel the long
		 * press timer, otherwise EC will crash.
		 */
		if (power_button_was_pressed)
			timer_cancel(TASK_ID_CHIPSET);

		/* Call hooks here since we don't know it prior to AP suspend */
		hook_notify(HOOK_CHIPSET_SUSPEND);
		enable_sleep(SLEEP_MASK_AP_RUN);
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

static void powerbtn_mtk_changed(void)
{
	task_wake(TASK_ID_CHIPSET);
}
DECLARE_HOOK(HOOK_POWER_BUTTON_CHANGE, powerbtn_mtk_changed, HOOK_PRIO_DEFAULT);

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
