/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * SDM845 SoC power sequencing module for Chrome EC
 *
 * This implements the following features:
 *
 * - Cold reset powers on the AP
 *
 *  When powered off:
 *  - Press power button turns on the AP
 *  - Hold power button turns on the AP, and then 8s later turns it off and
 *    leaves it off until pwron is released and pressed again
 *  - Lid open turns on the AP
 *
 *  When powered on:
 *  - Holding power button for 8s powers off the AP
 *  - Pressing and releasing pwron within that 8s is ignored
 *  - If POWER_GOOD is dropped by the AP, then we power the AP off
 */

#include "charge_state.h"
#include "chipset.h"
#include "common.h"
#include "gpio.h"
#include "hooks.h"
#include "lid_switch.h"
#include "power.h"
#include "power_button.h"
#include "system.h"
#include "task.h"
#include "util.h"

#define CPRINTS(format, args...) cprints(CC_CHIPSET, format, ## args)

/* Masks for power signals */
#define IN_POWER_GOOD POWER_SIGNAL_MASK(SDM845_POWER_GOOD)

/* Long power key press to force shutdown */
#define DELAY_FORCE_SHUTDOWN		(8 * SECOND)

/*
 * If the power button is pressed to turn on, then held for this long, we
 * power off.
 *
 * Normal case: User releases power button and chipset_task() goes
 *    into the inner loop, waiting for next event to occur (power button
 *    press or POWER_GOOD == 0).
 */
#define DELAY_SHUTDOWN_ON_POWER_HOLD	(8 * SECOND)

/*
 * After trigger PMIC power-on, how long it triggers AP to turn on.
 * Obversed that the worst case is ~150ms. Pick a safe vale.
 */
#define PMIC_POWER_AP_RESPONSE_TIMEOUT	(350 * MSEC)

/* Wait for polling the AP on signal */
#define PMIC_POWER_AP_WAIT		(1 * MSEC)

/* The timeout of the check if the system can boot AP */
#define CAN_BOOT_AP_CHECK_TIMEOUT	(500 * MSEC)

/* Wait for polling if the system can boot AP */
#define CAN_BOOT_AP_CHECK_WAIT		(100 * MSEC)

/* Delay between power-on the system and power-on the PMIC */
#define SYSTEM_POWER_ON_DELAY		(10 * MSEC)

/* Delay between power-off the system and all things (PMIC/AP) expected off */
#define SYSTEM_POWER_OFF_DELAY		(350 * MSEC)

/* Delay to confirm the power lost */
#define POWER_LOST_CONFIRM_DELAY        (350 * MSEC)

/* TODO(crosbug.com/p/25047): move to HOOK_POWER_BUTTON_CHANGE */
/* 1 if the power button was pressed last time we checked */
static char power_button_was_pressed;

/* 1 if lid-open event has been detected */
static char lid_opened;

/*
 * 1 if power state is controlled by special functions, like a console command
 * or an interrupt handler, for bypassing POWER_GOOD lost trigger. It is
 * because these functions control the PMIC and AP power signals directly and
 * don't want to get preempted by the chipset state machine.
 */
static uint8_t bypass_power_lost_trigger;

/* The timestamp of the latest power lost */
static timestamp_t latest_power_lost_time;

/* Time where we will power off, if power button still held down */
static timestamp_t power_off_deadline;

/* Force AP power on (used for recovery keypress) */
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

/* AP-requested reset GPIO interrupt handlers */
static void chipset_reset_request_handler(void)
{
	CPRINTS("AP wants reset");
	chipset_reset();
}
DECLARE_DEFERRED(chipset_reset_request_handler);

void chipset_reset_request_interrupt(enum gpio_signal signal)
{
	hook_call_deferred(&chipset_reset_request_handler_data, 0);
}

/* Confirm power lost if the POWER_GOOD signal keeps low for a while */
static uint32_t chipset_is_power_lost(void)
{
	/*
	 * Current POWER_GOOD signal is lost and the latest power lost trigger
	 * happened before the confirmation delay.
	 */
	return (get_time().val - latest_power_lost_time.val >=
		POWER_LOST_CONFIRM_DELAY) && !power_has_signals(IN_POWER_GOOD);
}

/* The deferred handler to save the power signal */
static void deferred_power_signal_handler(void)
{
	/* Wake the chipset task to check the power lost duration */
	task_wake(TASK_ID_CHIPSET);
}
DECLARE_DEFERRED(deferred_power_signal_handler);

/**
 * Power signal interrupt, overrides the default one.
 *
 * It handles the short-low-pulse during the reset sequence which we don't
 * consider it as a power-lost.
 */
void chipset_power_signal_interrupt(enum gpio_signal signal)
{
	/* Call the default power signal interrupt */
	power_signal_interrupt(signal);

	/*
	 * It is the start of the low pulse, save the timestamp, wake the
	 * chipset task after POWER_LOST_CONFIRM_DELAY in order to check if it
	 * is a power-lost or a reset (short low-pulse).
	 */
	if (!(power_get_signals() & IN_POWER_GOOD)) {
		/* Keep the timestamp just at the low pulse happens. */
		latest_power_lost_time = get_time();
		hook_call_deferred(&deferred_power_signal_handler_data,
				   POWER_LOST_CONFIRM_DELAY);
	}
}

static void sdm845_lid_event(void)
{
	/* Power task only cares about lid-open events */
	if (!lid_is_open())
		return;

	lid_opened = 1;
	task_wake(TASK_ID_CHIPSET);
}
DECLARE_HOOK(HOOK_LID_CHANGE, sdm845_lid_event, HOOK_PRIO_DEFAULT);

static void powerbtn_sdm845_changed(void)
{
	task_wake(TASK_ID_CHIPSET);
}
DECLARE_HOOK(HOOK_POWER_BUTTON_CHANGE, powerbtn_sdm845_changed,
	     HOOK_PRIO_DEFAULT);

/**
 * Set the state of the system power signals.
 *
 * The system power signals are the enable pins of SwitchCap and VBOB.
 * They control the power of the set of PMIC chips and the AP.
 *
 * @param enable	1 to enable or 0 to disable
 */
static void set_system_power(int enable)
{
	CPRINTS("set_system_power(%d)", enable);
	board_set_switchcap(enable);
	gpio_set_level(GPIO_VBOB_EN, enable);
}

/**
 * Get the state of the system power signals.
 *
 * @return 1 if the system is powered, 0 if not
 */
static int is_system_powered(void)
{
	return gpio_get_level(GPIO_SWITCHCAP_ON_L);
}

/**
 * Get the PMIC/AP power signal.
 *
 * We treat the PMIC chips and the AP as a whole here. Don't deal with
 * the individual chip.
 *
 * @return 1 if the PMIC/AP is powered, 0 if not
 */
static int is_pmic_pwron(void)
{
	/* Use PS_HOLD to indicate PMIC/AP is on/off */
	return gpio_get_level(GPIO_PS_HOLD);
}

/**
 * Wait the PMIC/AP power-on state.
 *
 * @param enable	1 to wait the PMIC/AP on.
			0 to wait the PMIC/AP off.
 */
static void wait_pmic_pwron(int enable)
{
	timestamp_t poll_deadline;

	/* Check the AP power status */
	if (enable == is_pmic_pwron())
		return;

	poll_deadline = get_time();
	poll_deadline.val += PMIC_POWER_AP_RESPONSE_TIMEOUT;
	while (enable != is_pmic_pwron() &&
	       get_time().val < poll_deadline.val) {
		usleep(PMIC_POWER_AP_WAIT);
	}

	/* Check the timeout case */
	if (enable != is_pmic_pwron()) {
		if (enable)
			CPRINTS("AP POWER NOT READY!");
		else
			CPRINTS("AP POWER STILL UP!");
	}
}

/**
 * Set the PMIC/AP power-on state.
 *
 * It triggers the PMIC/AP power-on and power-off sequence.
 *
 * @param enable	1 to power the PMIC/AP on.
			0 to power the PMIC/AP off.
 */
static void set_pmic_pwron(int enable)
{
	CPRINTS("set_pmic_pwron(%d)", enable);

	/* Check the PMIC/AP power state */
	if (enable == is_pmic_pwron())
		return;

	/*
	 * Power-on sequence:
	 * 1. Hold down PMIC_KPD_PWR_ODL, which is a power-on trigger
	 * 2. PM845 pulls up AP_RST_L signal to power-on SDM845
	 * 3. SDM845 pulls up PS_HOLD signal
	 * 4. Wait for PS_HOLD up
	 * 5. Release PMIC_KPD_PWR_ODL
	 *
	 * Power-off sequence:
	 * 1. Hold down PMIC_KPD_PWR_ODL and SYS_RST_L, which is a power-off
	 *    trigger (requiring reprogramming PMIC registers to make
	 *    PMIC_KPD_PWR_ODL + SYS_RST_L as a shutdown trigger)
	 * 2. PM845 pulls down AP_RST_L signal to power-off SDM845 (requreing
	 *    reprogramming PMIC to set the stage-1 and stage-2 reset timers to
	 *    0 such that the pull down happens just after the deboucing time
	 *    of the trigger, like 2ms)
	 * 3. SDM845 pulls down PS_HOLD signal
	 * 4. Wait for PS_HOLD down
	 * 5. Release PMIC_KPD_PWR_ODL and SYS_RST_L
	 *
	 * If the above PMIC registers not programmed or programmed wrong, it
	 * falls back to the next functions, which cuts off the system power.
	 */

	gpio_set_level(GPIO_PMIC_KPD_PWR_ODL, 0);
	if (!enable)
		gpio_set_level(GPIO_SYS_RST_L, 0);
	wait_pmic_pwron(enable);
	gpio_set_level(GPIO_PMIC_KPD_PWR_ODL, 1);
	if (!enable)
		gpio_set_level(GPIO_SYS_RST_L, 1);
}

enum power_state power_chipset_init(void)
{
	int init_power_state;
	uint32_t reset_flags = system_get_reset_flags();

	/* Enable reboot control input from AP */
	gpio_enable_interrupt(GPIO_AP_RST_REQ);

	/*
	 * Force the AP shutdown unless we are doing SYSJUMP. Otherwise,
	 * the AP could stay in strange state.
	 */
	if (!(reset_flags & RESET_FLAG_SYSJUMP)) {
		CPRINTS("not sysjump; forcing system shutdown");
		set_system_power(0);
		init_power_state = POWER_G3;
	} else {
		/* In the SYSJUMP case, we check if the AP is on */
		if (power_get_signals() & IN_POWER_GOOD) {
			CPRINTS("SOC ON");
			init_power_state = POWER_S0;
		} else {
			CPRINTS("SOC OFF");
			init_power_state = POWER_G3;
		}
	}

	/* Leave power off only if requested by reset flags */
	if (!(reset_flags & RESET_FLAG_AP_OFF) &&
	    !(reset_flags & RESET_FLAG_SYSJUMP)) {
		CPRINTS("auto_power_on set due to reset_flag 0x%x",
			system_get_reset_flags());
		auto_power_on = 1;
	}

	/*
	 * TODO(crosbug.com/p/28289): Wait battery stable.
	 * Some batteries use clock stretching feature, which requires
	 * more time to be stable.
	 */

	return init_power_state;
}

/*****************************************************************************/

/**
 * Power off the AP
 */
static void power_off(void)
{
	/* Check the power off status */
	if (!is_system_powered())
		return;

	/* Call hooks before we drop power rails */
	hook_notify(HOOK_CHIPSET_SHUTDOWN);

	/* Do a graceful way to shutdown PMIC/AP first */
	set_pmic_pwron(0);

	/* Force to switch off all rails */
	set_system_power(0);

	/* Wait longer to ensure the PMIC/AP totally off */
	usleep(SYSTEM_POWER_OFF_DELAY);

	/* Turn off the 5V rail. */
#ifdef CONFIG_POWER_PP5000_CONTROL
	power_5v_enable(task_get_current(), 0);
#else /* !defined(CONFIG_POWER_PP5000_CONTROL) */
	gpio_set_level(GPIO_EN_PP5000, 0);
#endif /* defined(CONFIG_POWER_PP5000_CONTROL) */

	lid_opened = 0;
	enable_sleep(SLEEP_MASK_AP_RUN);
	CPRINTS("power shutdown complete");
}

/**
 * Check if the power is enough to boot the AP.
 */
static int power_is_enough(void)
{
	timestamp_t poll_deadline;

	/* If powered by adapter only, wait a while for PD negoiation. */
	poll_deadline = get_time();
	poll_deadline.val += CAN_BOOT_AP_CHECK_TIMEOUT;

	/*
	 * Wait for PD negotiation. If a system with drained battery, don't
	 * waste the time and exit the loop.
	 */
	while (!system_can_boot_ap() && !charge_want_shutdown() &&
		get_time().val < poll_deadline.val) {
		usleep(CAN_BOOT_AP_CHECK_WAIT);
	}

	return system_can_boot_ap() && !charge_want_shutdown();
}

/**
 * Power on the AP
 */
static void power_on(void)
{
	/*
	 * If no enough power, return and the state machine will transition
	 * back to S5.
	 */
	if (!power_is_enough())
		return;

	/*
	 * When power_on() is called, we are at S5S3. Initialize components
	 * to ready state before AP is up.
	 */
	hook_notify(HOOK_CHIPSET_PRE_INIT);

	/* Enable the 5V rail. */
#ifdef CONFIG_POWER_PP5000_CONTROL
	power_5v_enable(task_get_current(), 1);
#else /* !defined(CONFIG_POWER_PP5000_CONTROL) */
	gpio_set_level(GPIO_EN_PP5000, 1);
#endif /* defined(CONFIG_POWER_PP5000_CONTROL) */

	set_system_power(1);
	usleep(SYSTEM_POWER_ON_DELAY);
	set_pmic_pwron(1);

	disable_sleep(SLEEP_MASK_AP_RUN);

	CPRINTS("AP running ...");
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
	if (power_get_signals() & IN_POWER_GOOD) {
		if (ap_off_flag) {
			CPRINTS("system is on, but RESET_FLAG_AP_OFF is on");
			return POWER_ON_CANCEL;
		}
		CPRINTS("system is on, thus clear auto_power_on");
		/* no need to arrange another power on */
		auto_power_on = 0;
		return POWER_ON_BY_IN_POWER_GOOD;
	}
	if (ap_off_flag) {
		CPRINTS("RESET_FLAG_AP_OFF is on");
		power_off();
		return POWER_ON_CANCEL;
	}

	CPRINTS("POWER_GOOD is not asserted");

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

	/* Power lost: shutdown immediately */
	if (chipset_is_power_lost() && !bypass_power_lost_trigger) {
		if (power_button_was_pressed)
			timer_cancel(TASK_ID_CHIPSET);

		CPRINTS("POWER_GOOD is lost");
		return POWER_OFF_BY_POWER_GOOD_LOST;
	}

	return POWER_OFF_CANCEL;
}

/*****************************************************************************/
/* Chipset interface */

void chipset_force_shutdown(void)
{
	CPRINTS("EC triggered shutdown");
	power_off();

	/* Clean-up internal variable */
	power_request = POWER_REQ_NONE;
}

void chipset_reset(void)
{
	/*
	 * Before we can reprogram the PMIC to make the PMIC RESIN_N pin as
	 * reset pin and zero-latency. We do cold reset instead.
	 */
	CPRINTS("EC triggered cold reboot");
	bypass_power_lost_trigger = 1;
	power_off();
	bypass_power_lost_trigger = 0;

	/* Issue a request to initiate a power-on sequence */
	power_request = POWER_REQ_ON;
	task_wake(TASK_ID_CHIPSET);
}

/**
 * Power handler for steady states
 *
 * @param state		Current power state
 * @return Updated power state
 */
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
		break;

	case POWER_S5S3:
		power_on();
		if (power_wait_signals(IN_POWER_GOOD) == EC_SUCCESS) {
			CPRINTS("POWER_GOOD seen");
			if (power_button_wait_for_release(
					DELAY_SHUTDOWN_ON_POWER_HOLD) ==
					EC_SUCCESS) {
				power_button_was_pressed = 0;

				/* Call hooks now that AP is running */
				hook_notify(HOOK_CHIPSET_STARTUP);

				return POWER_S3;
			}
			CPRINTS("long-press button, shutdown");
			power_off();
			/*
			 * Since the AP may be up already, return S0S3
			 * state to go through the suspend hook.
			 */
			return POWER_S0S3;
		}
		CPRINTS("POWER_GOOD not seen in time");
		set_system_power(0);
		return POWER_S5;

	case POWER_S3:
		if (!(power_get_signals() & IN_POWER_GOOD))
			return POWER_S3S5;

		/* Go to S3S0 directly, as don't know if it is in suspend */
		return POWER_S3S0;

	case POWER_S3S0:
		hook_notify(HOOK_CHIPSET_RESUME);
		return POWER_S0;

	case POWER_S0:
		value = check_for_power_off_event();
		if (value) {
			CPRINTS("power off %d", value);
			power_off();
			return POWER_S0S3;
		}
		break;

	case POWER_S0S3:
		/*
		 * If the power button is pressing, we need cancel the long
		 * press timer, otherwise EC will crash.
		 */
		if (power_button_was_pressed)
			timer_cancel(TASK_ID_CHIPSET);

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
	PSTATE_ON,
	PSTATE_COUNT,
};

static const char * const state_name[] = {
	"unknown",
	"off",
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
			"Turn AP power on/off");
