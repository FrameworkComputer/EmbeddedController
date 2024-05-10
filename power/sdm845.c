/* Copyright 2018 The ChromiumOS Authors
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

#include "builtin/assert.h"
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

#define CPRINTS(format, args...) cprints(CC_CHIPSET, format, ##args)

/* Masks for power signals */
#define IN_POWER_GOOD POWER_SIGNAL_MASK(SDM845_POWER_GOOD)
#define IN_AP_RST_ASSERTED POWER_SIGNAL_MASK(SDM845_AP_RST_ASSERTED)

/* Long power key press to force shutdown */
#define DELAY_FORCE_SHUTDOWN (8 * SECOND)

/*
 * If the power button is pressed to turn on, then held for this long, we
 * power off.
 *
 * Normal case: User releases power button and chipset_task() goes
 *    into the inner loop, waiting for next event to occur (power button
 *    press or POWER_GOOD == 0).
 */
#define DELAY_SHUTDOWN_ON_POWER_HOLD (8 * SECOND)

/*
 * After trigger PMIC power sequence, how long it triggers AP to turn on
 * or off. Observed that the worst case is ~150ms. Pick a safe vale.
 */
#define PMIC_POWER_AP_RESPONSE_TIMEOUT (350 * MSEC)

/*
 * After force off the switch cap, how long the PMIC/AP totally off.
 * Observed that the worst case is 2s. Pick a safe vale.
 */
#define FORCE_OFF_RESPONSE_TIMEOUT (4 * SECOND)

/* Wait for polling the AP on signal */
#define PMIC_POWER_AP_WAIT (1 * MSEC)

/* The length of an issued low pulse to the PMIC_RESIN_L signal */
#define PMIC_RESIN_PULSE_LENGTH (20 * MSEC)

/* The timeout of the check if the system can boot AP */
#define CAN_BOOT_AP_CHECK_TIMEOUT (500 * MSEC)

/* Wait for polling if the system can boot AP */
#define CAN_BOOT_AP_CHECK_WAIT (100 * MSEC)

/* The timeout of the check if the switchcap outputs good voltage */
#define SWITCHCAP_PG_CHECK_TIMEOUT (50 * MSEC)

/* Wait for polling if the switchcap outputs good voltage */
#define SWITCHCAP_PG_CHECK_WAIT (5 * MSEC)

/* Delay between power-on the system and power-on the PMIC */
#define SYSTEM_POWER_ON_DELAY (10 * MSEC)

/* TODO(crosbug.com/p/25047): move to HOOK_POWER_BUTTON_CHANGE */
/* 1 if the power button was pressed last time we checked */
static char power_button_was_pressed;

/* 1 if lid-open event has been detected */
static char lid_opened;

/* 1 if AP_RST_L and PS_HOLD is overdriven by EC */
static char ap_rst_overdriven;

/* Time where we will power off, if power button still held down */
static timestamp_t power_off_deadline;

/* Force AP power on (used for recovery keypress) */
static int auto_power_on;

enum power_request_t {
	POWER_REQ_NONE,
	POWER_REQ_OFF,
	POWER_REQ_ON,
	POWER_REQ_RESET,

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
	POWER_OFF_BY_POWER_REQ_OFF,
	POWER_OFF_BY_POWER_REQ_RESET,

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
	POWER_ON_BY_POWER_REQ_ON,
	POWER_ON_BY_POWER_REQ_RESET,

	POWER_ON_EVENT_COUNT,
};

/* Issue a request to initiate a reset sequence */
static void request_cold_reset(void)
{
	power_request = POWER_REQ_RESET;
	task_wake(TASK_ID_CHIPSET);
}

/* AP-requested reset GPIO interrupt handlers */
static void chipset_reset_request_handler(void)
{
	CPRINTS("AP wants reset");
	chipset_reset(CHIPSET_RESET_AP_REQ);
}
DECLARE_DEFERRED(chipset_reset_request_handler);

void chipset_reset_request_interrupt(enum gpio_signal signal)
{
	hook_call_deferred(&chipset_reset_request_handler_data, 0);
}

void chipset_warm_reset_interrupt(enum gpio_signal signal)
{
	/*
	 * The warm_reset signal is pulled-up by a rail from PMIC. If the
	 * warm_reset drops, it means:
	 *  * Servo or Cr50 holds the signal, or
	 *  * its pull-up rail POWER_GOOD drops.
	 */
	if (!gpio_get_level(GPIO_WARM_RESET_L)) {
		if (gpio_get_level(GPIO_POWER_GOOD)) {
			/*
			 * Servo or Cr50 holds the WARM_RESET_L signal.
			 *
			 * Overdrive AP_RST_L to hold AP. Overdrive PS_HOLD to
			 * emulate AP being up to trick the PMIC into thinking
			 * there’s nothing weird going on.
			 */
			ap_rst_overdriven = 1;
			gpio_set_flags(GPIO_PS_HOLD, GPIO_INT_BOTH |
							     GPIO_SEL_1P8V |
							     GPIO_OUT_HIGH);
			gpio_set_flags(GPIO_AP_RST_L, GPIO_INT_BOTH |
							      GPIO_SEL_1P8V |
							      GPIO_OUT_LOW);
		} else {
			/*
			 * The pull-up rail POWER_GOOD drops.
			 *
			 * High-Z both AP_RST_L and PS_HOLD to restore their
			 * states.
			 */
			gpio_set_flags(GPIO_AP_RST_L,
				       GPIO_INT_BOTH | GPIO_SEL_1P8V);
			gpio_set_flags(GPIO_PS_HOLD,
				       GPIO_INT_BOTH | GPIO_SEL_1P8V);
			ap_rst_overdriven = 0;
		}
	} else {
		if (ap_rst_overdriven) {
			/*
			 * Servo or Cr50 releases the WARM_RESET_L signal.
			 *
			 * Cold reset the PMIC, doing S0->S5->S0 transition,
			 * by issuing a request to initiate a reset sequence,
			 * to recover the system. The transition to S5 makes
			 * POWER_GOOD drop that triggers an interrupt to
			 * high-Z both AP_RST_L and PS_HOLD.
			 */
			request_cold_reset();
		}
		/* If not overdriven, just a normal power-up, do nothing. */
	}

	power_signal_interrupt(signal);
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
 * Wait the switchcap GPIO0 PVC_PG signal asserted.
 *
 * When the output voltage is over the threshold PVC_PG_ADJ,
 * the PVC_PG is asserted.
 *
 * PVG_PG_ADJ is configured to 3.0V.
 * GPIO0 is configured as PVC_PG.
 *
 * @param enable	1 to wait the PMIC/AP on.
			0 to wait the PMIC/AP off.
 */
static void wait_switchcap_power_good(int enable)
{
	timestamp_t poll_deadline;

	poll_deadline = get_time();
	poll_deadline.val += SWITCHCAP_PG_CHECK_TIMEOUT;
	while (enable != gpio_get_level(GPIO_DA9313_GPIO0) &&
	       get_time().val < poll_deadline.val) {
		crec_usleep(SWITCHCAP_PG_CHECK_WAIT);
	}

	/*
	 * Check the timeout case. Just show a message. More check later
	 * will switch the power state.
	 */
	if (enable != gpio_get_level(GPIO_DA9313_GPIO0)) {
		if (enable)
			CPRINTS("SWITCHCAP NO POWER GOOD!");
		else
			CPRINTS("SWITCHCAP STILL POWER GOOD!");
	}
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
	/* Use POWER_GOOD to indicate PMIC/AP is on/off */
	return gpio_get_level(GPIO_POWER_GOOD);
}

/**
 * Wait the PMIC/AP power-on state.
 *
 * @param enable	1 to wait the PMIC/AP on.
			0 to wait the PMIC/AP off.
 * @param timeout	Number of microsecond of timeout.
 */
static void wait_pmic_pwron(int enable, unsigned int timeout)
{
	timestamp_t poll_deadline;

	/* Check the AP power status */
	if (enable == is_pmic_pwron())
		return;

	poll_deadline = get_time();
	poll_deadline.val += timeout;
	while (enable != is_pmic_pwron() &&
	       get_time().val < poll_deadline.val) {
		crec_usleep(PMIC_POWER_AP_WAIT);
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
 * Set the state of the system power signals.
 *
 * The system power signals are the enable pins of SwitchCap and VBOB.
 * They control the power of the set of PMIC chips and the AP.
 *
 * @param enable	1 to enable or 0 to disable
 */
static void set_system_power(int enable)
{
	CPRINTS("%s(%d)", __func__, enable);
	gpio_set_level(GPIO_SWITCHCAP_ON_L, enable);
	wait_switchcap_power_good(enable);
	gpio_set_level(GPIO_VBOB_EN, enable);
	if (enable) {
		crec_usleep(SYSTEM_POWER_ON_DELAY);
	} else {
		/* Ensure POWER_GOOD drop to low if it is a forced shutdown */
		wait_pmic_pwron(0, FORCE_OFF_RESPONSE_TIMEOUT);
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
	CPRINTS("%s(%d)", __func__, enable);

	/* Check the PMIC/AP power state */
	if (enable == is_pmic_pwron())
		return;

	/*
	 * Power-on sequence:
	 * 1. Hold down PMIC_KPD_PWR_ODL, which is a power-on trigger
	 * 2. PM845 supplies power to POWER_GOOD
	 * 3. Release PMIC_KPD_PWR_ODL
	 *
	 * Power-off sequence:
	 * 1. Hold down PMIC_KPD_PWR_ODL and PMIC_RESIN_L, which is a power-off
	 *    trigger (requiring reprogramming PMIC registers to make
	 *    PMIC_KPD_PWR_ODL + PMIC_RESIN_L as a shutdown trigger)
	 * 2. PM845 stops supplying power to POWER_GOOD (requiring
	 *    reprogramming PMIC to set the stage-1 and stage-2 reset timers to
	 *    0 such that the pull down happens just after the deboucing time
	 *    of the trigger, like 2ms)
	 * 3. Release PMIC_KPD_PWR_ODL and PMIC_RESIN_L
	 *
	 * If the above PMIC registers not programmed or programmed wrong, it
	 * falls back to the next functions, which cuts off the system power.
	 */

	gpio_set_level(GPIO_PMIC_KPD_PWR_ODL, 0);
	if (!enable)
		gpio_set_level(GPIO_PMIC_RESIN_L, 0);
	wait_pmic_pwron(enable, PMIC_POWER_AP_RESPONSE_TIMEOUT);
	gpio_set_level(GPIO_PMIC_KPD_PWR_ODL, 1);
	if (!enable)
		gpio_set_level(GPIO_PMIC_RESIN_L, 1);
}

enum power_state power_chipset_init(void)
{
	int init_power_state;
	uint32_t reset_flags = system_get_reset_flags();

	/* Enable interrupts */
	gpio_enable_interrupt(GPIO_AP_RST_REQ);
	gpio_enable_interrupt(GPIO_WARM_RESET_L);
	gpio_enable_interrupt(GPIO_POWER_GOOD);

	/*
	 * Force the AP shutdown unless we are doing SYSJUMP. Otherwise,
	 * the AP could stay in strange state.
	 */
	if (!(reset_flags & EC_RESET_FLAG_SYSJUMP)) {
		CPRINTS("not sysjump; forcing system shutdown");
		set_system_power(0);
		init_power_state = POWER_G3;
	} else {
		/* In the SYSJUMP case, we check if the AP is on */
		if (power_get_signals() & IN_POWER_GOOD) {
			CPRINTS("SOC ON");
			init_power_state = POWER_S0;
			/* Disable idle task deep sleep when in S0 */
			disable_sleep(SLEEP_MASK_AP_RUN);
		} else {
			CPRINTS("SOC OFF");
			init_power_state = POWER_G3;
		}
	}

	/* Leave power off only if requested by reset flags */
	if (!(reset_flags & EC_RESET_FLAG_AP_OFF) &&
	    !(reset_flags & EC_RESET_FLAG_SYSJUMP)) {
		CPRINTS("auto_power_on set due to reset_flag 0x%x",
			system_get_reset_flags());
		auto_power_on = 1;
	}

	if (battery_is_present() == BP_YES) {
		/*
		 * (crosbug.com/p/28289): Wait battery stable.
		 * Some batteries use clock stretching feature, which requires
		 * more time to be stable.
		 */
		battery_wait_for_stable();
	}

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

	/* Disable signal interrupts, as they are floating when switchcap off */
	power_signal_disable_interrupt(GPIO_AP_RST_L);
	power_signal_disable_interrupt(GPIO_PMIC_FAULT_L);

	/* Force to switch off all rails */
	set_system_power(0);

	/* Turn off the 3.3V and 5V rails. */
	gpio_set_level(GPIO_EN_PP3300_A, 0);
#ifdef CONFIG_POWER_PP5000_CONTROL
	power_5v_enable(task_get_current(), 0);
#else /* !defined(CONFIG_POWER_PP5000_CONTROL) */
	gpio_set_level(GPIO_EN_PP5000, 0);
#endif /* defined(CONFIG_POWER_PP5000_CONTROL) */

	lid_opened = 0;
	enable_sleep(SLEEP_MASK_AP_RUN);
	CPRINTS("power shutdown complete");

	/* Call hooks after we drop power rails */
	hook_notify(HOOK_CHIPSET_SHUTDOWN_COMPLETE);
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
		crec_usleep(CAN_BOOT_AP_CHECK_WAIT);
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

	/* Enable the 3.3V and 5V rail. */
	gpio_set_level(GPIO_EN_PP3300_A, 1);
#ifdef CONFIG_POWER_PP5000_CONTROL
	power_5v_enable(task_get_current(), 1);
#else /* !defined(CONFIG_POWER_PP5000_CONTROL) */
	gpio_set_level(GPIO_EN_PP5000, 1);
#endif /* defined(CONFIG_POWER_PP5000_CONTROL) */

	set_system_power(1);

	/* Enable signal interrupts */
	power_signal_enable_interrupt(GPIO_AP_RST_L);
	power_signal_enable_interrupt(GPIO_PMIC_FAULT_L);

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
static uint8_t check_for_power_on_event(void)
{
	int ap_off_flag;

	ap_off_flag = system_get_reset_flags() & EC_RESET_FLAG_AP_OFF;
	system_clear_reset_flags(EC_RESET_FLAG_AP_OFF);

	if (power_request == POWER_REQ_ON) {
		power_request = POWER_REQ_NONE;
		return POWER_ON_BY_POWER_REQ_ON;
	} else if (power_request == POWER_REQ_RESET) {
		power_request = POWER_REQ_NONE;
		return POWER_ON_BY_POWER_REQ_RESET;
	}
	/* Clear invalid request */
	power_request = POWER_REQ_NONE;

	/* check if system is already ON */
	if (power_get_signals() & IN_POWER_GOOD) {
		if (ap_off_flag) {
			CPRINTS("system is on, but EC_RESET_FLAG_AP_OFF is on");
			return POWER_ON_CANCEL;
		}
		CPRINTS("system is on, thus clear auto_power_on");
		/* no need to arrange another power on */
		auto_power_on = 0;
		return POWER_ON_BY_IN_POWER_GOOD;
	}
	if (ap_off_flag) {
		CPRINTS("EC_RESET_FLAG_AP_OFF is on");
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
static uint8_t check_for_power_off_event(void)
{
	timestamp_t now;
	int pressed = 0;

	if (power_request == POWER_REQ_OFF) {
		power_request = POWER_REQ_NONE;
		return POWER_OFF_BY_POWER_REQ_OFF;
	} else if (power_request == POWER_REQ_RESET) {
		/*
		 * The power_request flag will be cleared later
		 * in check_for_power_on_event() in S5.
		 */
		return POWER_OFF_BY_POWER_REQ_RESET;
	}
	/* Clear invalid request */
	power_request = POWER_REQ_NONE;

	/*
	 * Check for power button press.
	 */
	if (power_button_is_pressed())
		pressed = POWER_OFF_BY_POWER_BUTTON_PRESSED;

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

	/* POWER_GOOD released by AP : shutdown immediately */
	if (!power_has_signals(IN_POWER_GOOD)) {
		if (power_button_was_pressed)
			timer_cancel(TASK_ID_CHIPSET);

		CPRINTS("POWER_GOOD is lost");
		return POWER_OFF_BY_POWER_GOOD_LOST;
	}

	return POWER_OFF_CANCEL;
}

/*****************************************************************************/
/* Chipset interface */

void chipset_force_shutdown(enum chipset_shutdown_reason reason)
{
	CPRINTS("%s(%d)", __func__, reason);
	report_ap_reset(reason);

	/* Issue a request to initiate a power-off sequence */
	power_request = POWER_REQ_OFF;
	task_wake(TASK_ID_CHIPSET);
}

void chipset_reset(enum chipset_shutdown_reason reason)
{
	int rv;

	CPRINTS("%s(%d)", __func__, reason);
	report_ap_reset(reason);

	/*
	 * Warm reset sequence:
	 * 1. Issue a low pulse to PMIC_RESIN_L, which triggers PMIC
	 *    to do a warm reset (requiring reprogramming PMIC registers
	 *    to make PMIC_RESIN_L as a warm reset trigger).
	 * 2. PMIC then issues a low pulse to AP_RST_L to reset AP.
	 *    EC monitors the signal to see any low pulse.
	 *    2.1. If a low pulse found, done.
	 *    2.2. If a low pulse not found (the above PMIC registers
	 *         not programmed or programmed wrong), issue a request
	 *         to initiate a cold reset power sequence.
	 */

	gpio_set_level(GPIO_PMIC_RESIN_L, 0);
	crec_usleep(PMIC_RESIN_PULSE_LENGTH);
	gpio_set_level(GPIO_PMIC_RESIN_L, 1);

	rv = power_wait_signals_timeout(IN_AP_RST_ASSERTED,
					PMIC_POWER_AP_RESPONSE_TIMEOUT);
	/* Exception case: PMIC not work as expected, request a cold reset */
	if (rv != EC_SUCCESS)
		request_cold_reset();
}

/**
 * Power handler for steady states
 *
 * @param state		Current power state
 * @return Updated power state
 */
enum power_state power_handle_state(enum power_state state)
{
	uint8_t value;
	static uint8_t boot_from_g3, shutdown_from_s0;

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
		/*
		 * Wait for power button release before actually boot AP.
		 * It may be a long-hold power button with volume buttons
		 * to trigger the recovery button. We don't want AP up
		 * during the long-hold.
		 */
		power_button_wait_for_release(-1);

		power_on();
		if (power_wait_signals(IN_POWER_GOOD) != EC_SUCCESS) {
			CPRINTS("POWER_GOOD not seen in time");
			set_system_power(0);
			return POWER_S5;
		}

		CPRINTS("POWER_GOOD seen");
		/* Call hooks now that AP is running */
		hook_notify(HOOK_CHIPSET_STARTUP);
		return POWER_S3;

	case POWER_S3:
		if (shutdown_from_s0) {
			value = shutdown_from_s0;
			shutdown_from_s0 = 0;
		} else {
			value = check_for_power_off_event();
		}

		if (value) {
			CPRINTS("power off %d", value);
			return POWER_S3S5;
		}
		/* Go to S3S0 directly, as don't know if it is in suspend */
		return POWER_S3S0;

	case POWER_S3S0:
		hook_notify(HOOK_CHIPSET_RESUME);
		return POWER_S0;

	case POWER_S0:
		shutdown_from_s0 = check_for_power_off_event();
		if (shutdown_from_s0)
			return POWER_S0S3;
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
		power_off();
		/*
		 * Wait forever for the release of the power button; otherwise,
		 * this power button press will then trigger a power-on in S5.
		 */
		power_button_wait_for_release(-1);
		power_button_was_pressed = 0;
		return POWER_S5;

	case POWER_S5G3:
		return POWER_G3;

	default:
		CPRINTS("Unexpected power state %d", state);
		ASSERT(0);
		break;
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

static const char *const state_name[] = {
	"unknown",
	"off",
	"on",
};

static int command_power(int argc, const char **argv)
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
DECLARE_CONSOLE_COMMAND(power, command_power, "on/off", "Turn AP power on/off");
