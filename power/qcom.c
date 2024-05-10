/* Copyright 2019 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * SC7X80 SoC power sequencing module for Chrome EC
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
#include "power/qcom.h"
#include "power_button.h"
#include "system.h"
#include "task.h"
#include "util.h"

#define CPRINTS(format, args...) cprints(CC_CHIPSET, format, ##args)

/* Power signal list. Must match order of enum power_signal. */
const struct power_signal_info power_signal_list[] = {
	[SC7X80_AP_RST_ASSERTED] = {
		GPIO_AP_RST_L,
		POWER_SIGNAL_ACTIVE_LOW | POWER_SIGNAL_DISABLE_AT_BOOT,
		"AP_RST_ASSERTED",
	},
	[SC7X80_PS_HOLD] = {
		GPIO_PS_HOLD,
		POWER_SIGNAL_ACTIVE_HIGH,
		"PS_HOLD",
	},
	[SC7X80_POWER_GOOD] = {
		GPIO_POWER_GOOD,
		POWER_SIGNAL_ACTIVE_HIGH,
		"POWER_GOOD",
	},
	[SC7X80_AP_SUSPEND] = {
		GPIO_AP_SUSPEND,
		POWER_SIGNAL_ACTIVE_HIGH | POWER_SIGNAL_NO_LOG,
		"AP_SUSPEND",
	},
#ifdef CONFIG_CHIPSET_SC7180
	[SC7X80_WARM_RESET] = {
		GPIO_WARM_RESET_L,
		POWER_SIGNAL_ACTIVE_HIGH,
		"WARM_RESET_L",
	},
	[SC7X80_DEPRECATED_AP_RST_REQ] = {
		GPIO_DEPRECATED_AP_RST_REQ,
		POWER_SIGNAL_ACTIVE_HIGH,
		"DEPRECATED_AP_RST_REQ",
	},
#endif /* defined(CONFIG_CHIPSET_SC7180) */
};
BUILD_ASSERT(ARRAY_SIZE(power_signal_list) == POWER_SIGNAL_COUNT);

/* Masks for power signals */
#define IN_POWER_GOOD POWER_SIGNAL_MASK(SC7X80_POWER_GOOD)
#define IN_AP_RST_ASSERTED POWER_SIGNAL_MASK(SC7X80_AP_RST_ASSERTED)
#define IN_SUSPEND POWER_SIGNAL_MASK(SC7X80_AP_SUSPEND)

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
#define CAN_BOOT_AP_CHECK_TIMEOUT (1500 * MSEC)

/* Wait for polling if the system can boot AP */
#define CAN_BOOT_AP_CHECK_WAIT (200 * MSEC)

/* The timeout of the check if the switchcap outputs good voltage */
#define SWITCHCAP_PG_CHECK_TIMEOUT (100 * MSEC)

/* Wait for polling if the switchcap outputs good voltage */
#define SWITCHCAP_PG_CHECK_WAIT (6 * MSEC)

/*
 * Delay between power-on the system and power-on the PMIC.
 * Some latest PMIC firmware needs this delay longer, for doing a cold
 * reboot.
 *
 * Measured on Herobrine IOB + Trogdor MLB, the delay takes ~200ms. Set
 * it with margin.
 */
#define SYSTEM_POWER_ON_DELAY (300 * MSEC)

/*
 * Delay between the PMIC power drop and power-off the system.
 * Qualcomm measured the entire POFF duration is around 70ms. Setting
 * this delay to the same value as the above power-on sequence, which
 * has much safer margin.
 */
#define PMIC_POWER_OFF_DELAY (150 * MSEC)

/* The AP_RST_L transition count of a normal AP warm reset */
#define EXPECTED_AP_RST_TRANSITIONS 3

/*
 * The timeout of waiting the next AP_RST_L transition. We measured
 * the interval between AP_RST_L transitions is 130ms ~ 150ms. Pick
 * a safer value.
 */
#define AP_RST_TRANSITION_TIMEOUT (450 * MSEC)

/* TODO(crosbug.com/p/25047): move to HOOK_POWER_BUTTON_CHANGE */
/* 1 if the power button was pressed last time we checked */
static char power_button_was_pressed;

/* 1 if lid-open event has been detected */
static char lid_opened;

/* Time where we will power off, if power button still held down */
static timestamp_t power_off_deadline;

/* Force AP power on (used for recovery keypress) */
static int auto_power_on;

enum power_request_t {
	POWER_REQ_NONE,
	POWER_REQ_OFF,
	POWER_REQ_ON,
	POWER_REQ_COLD_RESET,
	POWER_REQ_WARM_RESET,

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
	POWER_ON_BY_AUTO_POWER_ON,
	POWER_ON_BY_LID_OPEN,
	POWER_ON_BY_POWER_BUTTON_PRESSED,
	POWER_ON_BY_POWER_REQ_ON,
	POWER_ON_BY_POWER_REQ_RESET,

	POWER_ON_EVENT_COUNT,
};

#ifdef CONFIG_CHIPSET_RESET_HOOK
static int ap_rst_transitions;

static void notify_chipset_reset(void)
{
	if (ap_rst_transitions != EXPECTED_AP_RST_TRANSITIONS)
		CPRINTS("AP_RST_L transitions not expected: %d",
			ap_rst_transitions);

	ap_rst_transitions = 0;
	hook_notify(HOOK_CHIPSET_RESET);
}
DECLARE_DEFERRED(notify_chipset_reset);
#endif

void chipset_ap_rst_interrupt(enum gpio_signal signal)
{
#ifdef CONFIG_CHIPSET_RESET_HOOK
	int delay;

	/*
	 * Only care the raising edge and AP in S0/S3. The single raising edge
	 * of AP power-on during S5S3 is ignored.
	 */
	if (gpio_get_level(GPIO_AP_RST_L) &&
	    chipset_in_state(CHIPSET_STATE_ON | CHIPSET_STATE_SUSPEND)) {
		ap_rst_transitions++;
		if (ap_rst_transitions >= EXPECTED_AP_RST_TRANSITIONS) {
			/*
			 * Reach the expected transition count. AP is booting
			 * up. Notify HOOK_CHIPSET_RESET immediately.
			 */
			delay = 0;
		} else {
			/*
			 * Should have more transitions of the AP_RST_L signal.
			 * In case the AP_RST_L signal is not toggled, still
			 * notify HOOK_CHIPSET_RESET.
			 */
			delay = AP_RST_TRANSITION_TIMEOUT;
		}
		hook_call_deferred(&notify_chipset_reset_data, delay);
	}
#endif
	power_signal_interrupt(signal);
}

#ifdef CONFIG_CHIPSET_SC7180

/* 1 if AP_RST_L and PS_HOLD is overdriven by EC */
static char ap_rst_overdriven;

/* Issue a request to initiate a reset sequence */
static void request_cold_reset(void)
{
	power_request = POWER_REQ_COLD_RESET;
	task_wake(TASK_ID_CHIPSET);
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
		}
		/* Ignore the else clause, the pull-up rail drops. */
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
			CPRINTS("Long warm reset ended, "
				"cold resetting to restore confidence.");
			request_cold_reset();
		}
		/* If not overdriven, just a normal power-up, do nothing. */
	}
	power_signal_interrupt(signal);
}

void chipset_power_good_interrupt(enum gpio_signal signal)
{
	if (!gpio_get_level(GPIO_POWER_GOOD) && ap_rst_overdriven) {
		/*
		 * POWER_GOOD is the pull-up rail of WARM_RESET_L.
		 * When POWER_GOOD drops, high-Z both AP_RST_L and PS_HOLD
		 * to restore their states.
		 */
		gpio_set_flags(GPIO_AP_RST_L, GPIO_INT_BOTH | GPIO_SEL_1P8V);
		gpio_set_flags(GPIO_PS_HOLD, GPIO_INT_BOTH | GPIO_SEL_1P8V);
		ap_rst_overdriven = 0;
	}
	power_signal_interrupt(signal);
}
#endif /* defined(CONFIG_CHIPSET_SC7180) */

static void sc7x80_lid_event(void)
{
	/* Power task only cares about lid-open events */
	if (!lid_is_open())
		return;

	lid_opened = 1;
	task_wake(TASK_ID_CHIPSET);
}
DECLARE_HOOK(HOOK_LID_CHANGE, sc7x80_lid_event, HOOK_PRIO_DEFAULT);

static void sc7x80_powerbtn_changed(void)
{
	task_wake(TASK_ID_CHIPSET);
}
DECLARE_HOOK(HOOK_POWER_BUTTON_CHANGE, sc7x80_powerbtn_changed,
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
 *			0 to wait the PMIC/AP off.
 *
 * @return EC_SUCCESS or error
 */
static int wait_switchcap_power_good(int enable)
{
	timestamp_t poll_deadline;

	poll_deadline = get_time();
	poll_deadline.val += SWITCHCAP_PG_CHECK_TIMEOUT;
	while (enable != board_is_switchcap_power_good() &&
	       get_time().val < poll_deadline.val) {
		crec_usleep(SWITCHCAP_PG_CHECK_WAIT);
	}

	/*
	 * Check the timeout case. Just show a message. More check later
	 * will switch the power state.
	 */
	if (enable != board_is_switchcap_power_good()) {
		if (enable)
			CPRINTS("SWITCHCAP NO POWER GOOD!");
		else
			CPRINTS("SWITCHCAP STILL POWER GOOD!");
		return EC_ERROR_UNKNOWN;
	}
	return EC_SUCCESS;
}

/**
 * Get the state of the system power signals.
 *
 * @return 1 if the system is powered, 0 if not
 */
static int is_system_powered(void)
{
	return board_is_switchcap_enabled();
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
 *			0 to wait the PMIC/AP off.
 * @param timeout	Number of microsecond of timeout.
 *
 * @return EC_SUCCESS or error
 */
static int wait_pmic_pwron(int enable, unsigned int timeout)
{
	timestamp_t poll_deadline;

	/* Check the AP power status */
	if (enable == is_pmic_pwron())
		return EC_SUCCESS;

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

		return EC_ERROR_UNKNOWN;
	}
	return EC_SUCCESS;
}

/**
 * Set the state of the system power signals but without any check.
 *
 * The system power signals are the enable pins of SwitchCap.
 * They control the power of the set of PMIC chips and the AP.
 *
 * @param enable	1 to enable or 0 to disable
 */
static void set_system_power_no_check(int enable)
{
	board_set_switchcap_power(enable);
}

/**
 * Set the state of the system power signals.
 *
 * The system power signals are the enable pins of SwitchCap.
 * They control the power of the set of PMIC chips and the AP.
 *
 * @param enable	1 to enable or 0 to disable
 *
 * @return EC_SUCCESS or error
 */
static int set_system_power(int enable)
{
	int ret;

	CPRINTS("%s(%d)", __func__, enable);
	set_system_power_no_check(enable);

	ret = wait_switchcap_power_good(enable);

	if (!enable) {
		/* Ensure POWER_GOOD drop to low if it is a forced shutdown */
		ret |= wait_pmic_pwron(0, FORCE_OFF_RESPONSE_TIMEOUT);
	}
	crec_usleep(SYSTEM_POWER_ON_DELAY);

	return ret;
}

/**
 * Set the PMIC/AP power-on state.
 *
 * It triggers the PMIC/AP power-on and power-off sequence.
 *
 * @param enable	1 to power the PMIC/AP on.
 *			0 to power the PMIC/AP off.
 *
 * @return EC_SUCCESS or error
 */
static int set_pmic_pwron(int enable)
{
	int ret;

	CPRINTS("%s(%d)", __func__, enable);

	/* Check the PMIC/AP power state */
	if (enable == is_pmic_pwron())
		return EC_SUCCESS;

	if (!gpio_get_level(GPIO_PMIC_RESIN_L)) {
		CPRINTS("PMIC_RESIN_L not pulled up by PMIC; cancel pwron");
		return EC_ERROR_UNKNOWN;
	}

	/*
	 * Power-on sequence:
	 * 1. Hold down PMIC_KPD_PWR_ODL, which is a power-on trigger
	 * 2. PMIC supplies power to POWER_GOOD
	 * 3. Release PMIC_KPD_PWR_ODL
	 *
	 * Power-off sequence:
	 * 1. Hold down PMIC_KPD_PWR_ODL and PMIC_RESIN_L, which is a power-off
	 *    trigger (requiring reprogramming PMIC registers to make
	 *    PMIC_KPD_PWR_ODL + PMIC_RESIN_L as a shutdown trigger)
	 * 2. PMIC stops supplying power to POWER_GOOD (requiring
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
	ret = wait_pmic_pwron(enable, PMIC_POWER_AP_RESPONSE_TIMEOUT);
	gpio_set_level(GPIO_PMIC_KPD_PWR_ODL, 1);
	if (!enable)
		gpio_set_level(GPIO_PMIC_RESIN_L, 1);

	return ret;
}

enum power_state power_chipset_init(void)
{
	int init_power_state;
	uint32_t reset_flags = system_get_reset_flags();

	/* Enable interrupts */
	if (IS_ENABLED(CONFIG_CHIPSET_SC7180)) {
		gpio_enable_interrupt(GPIO_WARM_RESET_L);
		gpio_enable_interrupt(GPIO_POWER_GOOD);
	}

	/*
	 * Force the AP shutdown unless we are doing SYSJUMP. Otherwise,
	 * the AP could stay in strange state.
	 */
	if (!(reset_flags & EC_RESET_FLAG_SYSJUMP)) {
		CPRINTS("not sysjump; forcing system shutdown");
		set_system_power_no_check(0);
		init_power_state = POWER_G3;
	} else {
		/* In the SYSJUMP case, we check if the AP is on */
		if (power_get_signals() & IN_POWER_GOOD) {
			CPRINTS("SOC ON");
			init_power_state = POWER_S0;

			/*
			 * Reenable the power signal AP_RST_L interrupt, which
			 * should be enabled during S5->S3 but sysjump makes
			 * it back to default, disabled.
			 */
			power_signal_enable_interrupt(GPIO_AP_RST_L);

			/* Disable idle task deep sleep when in S0 */
			disable_sleep(SLEEP_MASK_AP_RUN);
		} else {
			CPRINTS("SOC OFF");
			init_power_state = POWER_G3;
		}
	}

	auto_power_on = 1;

	/*
	 * Leave power off only if requested by reset flags
	 *
	 * TODO(b/201099749): EC bootloader: Give RO chance to run EFS after
	 * shutdown from recovery screen
	 */
	if (reset_flags & EC_RESET_FLAG_AP_OFF)
		auto_power_on = 0;
	else if (!(reset_flags & EC_RESET_FLAG_EFS) &&
		 (reset_flags & EC_RESET_FLAG_SYSJUMP))
		auto_power_on = 0;

	if (battery_is_present() == BP_YES) {
		/*
		 * (crosbug.com/p/28289): Wait battery stable.
		 * Some batteries use clock stretching feature, which requires
		 * more time to be stable.
		 */
		battery_wait_for_stable();
	}

	if (auto_power_on)
		CPRINTS("auto_power_on set due to reset flags");

	return init_power_state;
}

/*****************************************************************************/

/**
 * Power off the AP
 *
 * @param shutdown_event	reason of shutdown, which is a return value of
 *				check_for_power_off_event()
 */
static void power_off_seq(uint8_t shutdown_event)
{
	/* Check PMIC POWER_GOOD */
	if (is_pmic_pwron()) {
		if (shutdown_event == POWER_OFF_BY_POWER_GOOD_LOST) {
			/*
			 * The POWER_GOOD was lost previously, which sets the
			 * shutdown_event flag. But now it is up again. This
			 * is unexpected. Show the warning message. Then go
			 * straight to turn off the switchcap.
			 */
			CPRINTS("Warning: POWER_GOOD up again after lost");
		} else {
			/* Do a graceful way to shutdown PMIC/AP first */
			set_pmic_pwron(0);
			crec_usleep(PMIC_POWER_OFF_DELAY);
		}
	}

	/*
	 * Disable signal interrupts, as they are floating when
	 * switchcap off.
	 */
	power_signal_disable_interrupt(GPIO_AP_RST_L);

	/* Check the switchcap status */
	if (is_system_powered()) {
		/* Force to switch off all rails */
		set_system_power(0);
	}

	lid_opened = 0;
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
 *
 * @return EC_SUCCESS or error
 */
static int power_on_seq(void)
{
	int ret;

	ret = set_system_power(1);
	if (ret != EC_SUCCESS)
		return ret;

	/* Enable signal interrupts */
	power_signal_enable_interrupt(GPIO_AP_RST_L);

	ret = set_pmic_pwron(1);
	if (ret != EC_SUCCESS) {
		CPRINTS("POWER_GOOD not seen in time");
		return ret;
	}

	CPRINTS("POWER_GOOD seen");
	return EC_SUCCESS;
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
	uint8_t ret;

	if (power_request == POWER_REQ_ON) {
		ret = POWER_ON_BY_POWER_REQ_ON;
	} else if (power_request == POWER_REQ_COLD_RESET) {
		ret = POWER_ON_BY_POWER_REQ_RESET;
	} else if (auto_power_on) {
		/* power on requested at EC startup for recovery */
		ret = POWER_ON_BY_AUTO_POWER_ON;
	} else if (lid_opened) {
		/* check lid open */
		ret = POWER_ON_BY_LID_OPEN;
	} else if (power_button_is_pressed()) {
		/* check for power button press */
		ret = POWER_ON_BY_POWER_BUTTON_PRESSED;
	} else {
		ret = POWER_OFF_CANCEL;
	}

	/* The flags are handled above. Clear them all. */
	power_request = POWER_REQ_NONE;
	auto_power_on = 0;
	lid_opened = 0;

	return ret;
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
	} else if (power_request == POWER_REQ_COLD_RESET) {
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
		CPRINTS("POWER_GOOD is lost");
		return POWER_OFF_BY_POWER_GOOD_LOST;
	}

	return POWER_OFF_CANCEL;
}

/**
 * Cancel the power button timer.
 *
 * The timer was previously created in the check_for_power_off_event(),
 * which waited for the power button long press. Should cancel the timer
 * during the power state transition; otherwise, EC will crash.
 */
static inline void cancel_power_button_timer(void)
{
	if (power_button_was_pressed)
		timer_cancel(TASK_ID_CHIPSET);
}

/*****************************************************************************/
/* Chipset interface */

test_mockable void chipset_force_shutdown(enum chipset_shutdown_reason reason)
{
	CPRINTS("%s(%d)", __func__, reason);
	report_ap_reset(reason);

	/* Issue a request to initiate a power-off sequence */
	power_request = POWER_REQ_OFF;
	task_wake(TASK_ID_CHIPSET);
}

test_mockable void chipset_power_on(void)
{
	if (chipset_in_state(CHIPSET_STATE_ANY_OFF)) {
		power_request = POWER_REQ_ON;
		task_wake(TASK_ID_CHIPSET);
	}
}

/**
 * Warm reset the AP
 *
 * @return EC_SUCCESS or error
 */
static int warm_reset_seq(void)
{
	int rv;

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
		return rv;

	return EC_SUCCESS;
}

/**
 * Check for some event triggering the warm reset.
 *
 * The only event is a request by the console command `apreset`.
 */
static void check_for_warm_reset_event(void)
{
	int rv;

	if (power_request == POWER_REQ_WARM_RESET) {
		power_request = POWER_REQ_NONE;
		rv = warm_reset_seq();
		if (rv != EC_SUCCESS) {
			CPRINTS("AP refuses to warm reset. Cold resetting.");
			power_request = POWER_REQ_COLD_RESET;
		}
	}
}

test_mockable void chipset_reset(enum chipset_shutdown_reason reason)
{
	CPRINTS("%s(%d)", __func__, reason);
	report_ap_reset(reason);

	power_request = POWER_REQ_WARM_RESET;
	task_wake(TASK_ID_CHIPSET);
}

/* Get system sleep state through GPIOs */
static inline int chipset_get_sleep_signal(void)
{
	return (power_get_signals() & IN_SUSPEND) == IN_SUSPEND;
}

__override void power_chipset_handle_sleep_hang(enum sleep_hang_type hang_type)
{
	CPRINTS("Warning: Detected sleep hang! Waking host up!");
	host_set_single_event(EC_HOST_EVENT_HANG_DETECT);
}

static void power_reset_host_sleep_state(void)
{
	power_set_host_sleep_state(HOST_SLEEP_EVENT_DEFAULT_RESET);
	sleep_reset_tracking();
	power_chipset_handle_host_sleep_event(HOST_SLEEP_EVENT_DEFAULT_RESET,
					      NULL);
}

static void handle_chipset_reset(void)
{
	if (chipset_in_state(CHIPSET_STATE_SUSPEND)) {
		CPRINTS("Chipset reset: exit s3");
		power_reset_host_sleep_state();
		task_wake(TASK_ID_CHIPSET);
	}
}
DECLARE_HOOK(HOOK_CHIPSET_RESET, handle_chipset_reset, HOOK_PRIO_FIRST);

__override void
power_chipset_handle_host_sleep_event(enum host_sleep_event state,
				      struct host_sleep_event_context *ctx)
{
	CPRINTS("Handle sleep: %d", state);

	if (state == HOST_SLEEP_EVENT_S3_SUSPEND) {
		/*
		 * Indicate to power state machine that a new host event for
		 * S3 suspend has been received and so chipset suspend
		 * notification needs to be sent to listeners.
		 */
		sleep_set_notify(SLEEP_NOTIFY_SUSPEND);
		sleep_start_suspend(ctx);
		power_signal_enable_interrupt(GPIO_AP_SUSPEND);

	} else if (state == HOST_SLEEP_EVENT_S3_RESUME) {
		/*
		 * In case the suspend fails, cancel the power button timer,
		 * similar to what we do in S3S0, the suspend success case.
		 */
		cancel_power_button_timer();
		/*
		 * Wake up chipset task and indicate to power state machine that
		 * listeners need to be notified of chipset resume.
		 */
		sleep_set_notify(SLEEP_NOTIFY_RESUME);
		task_wake(TASK_ID_CHIPSET);
		power_signal_disable_interrupt(GPIO_AP_SUSPEND);
		sleep_complete_resume(ctx);

	} else if (state == HOST_SLEEP_EVENT_DEFAULT_RESET) {
		power_signal_disable_interrupt(GPIO_AP_SUSPEND);
	}
}

/**
 * Power handler for steady states
 *
 * @param state		Current power state
 * @return Updated power state
 */
test_mockable enum power_state power_handle_state(enum power_state state)
{
	static uint8_t boot_from_off, shutdown_from_on;

	switch (state) {
	case POWER_G3:
		boot_from_off = check_for_power_on_event();
		if (boot_from_off)
			return POWER_G3S5;
		break;

	case POWER_G3S5:
		return POWER_S5;

	case POWER_S5:
		if (!boot_from_off)
			boot_from_off = check_for_power_on_event();

		if (boot_from_off) {
			CPRINTS("power on %d", boot_from_off);
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

		/* If no enough power, return back to S5. */
		if (!power_is_enough()) {
			boot_from_off = 0;
			return POWER_S5;
		}

		/* Initialize components to ready state before AP is up. */
		hook_notify(HOOK_CHIPSET_PRE_INIT);

		if (power_on_seq() != EC_SUCCESS) {
			power_off_seq(shutdown_from_on);
			boot_from_off = 0;
			return POWER_S5;
		}
		CPRINTS("AP running ...");

		/* Call hooks now that AP is running */
		hook_notify(HOOK_CHIPSET_STARTUP);

		/*
		 * Clearing the sleep failure detection tracking on the path
		 * to S0 to handle any reset conditions.
		 */
		power_reset_host_sleep_state();
		return POWER_S3;

	case POWER_S3:
		if (!shutdown_from_on)
			shutdown_from_on = check_for_power_off_event();

		if (shutdown_from_on) {
			CPRINTS("power off %d", shutdown_from_on);
			return POWER_S3S5;
		}

		/*
		 * AP has woken up and it deasserts the suspend signal;
		 * go to S0.
		 *
		 * In S0, it will wait for a host event and then trigger the
		 * RESUME hook.
		 */
		if (!chipset_get_sleep_signal())
			return POWER_S3S0;
		break;

	case POWER_S3S0:
		cancel_power_button_timer();

#ifdef CONFIG_CHIPSET_RESUME_INIT_HOOK
		/*
		 * Notify the RESUME_INIT hooks, i.e. enabling SPI driver
		 * to receive host commands/events.
		 *
		 * If boot from an off state, notify the RESUME hooks too;
		 * otherwise (resume from S3), the normal RESUME hooks will
		 * be notified later, after receive a host resume event.
		 */
		hook_notify(HOOK_CHIPSET_RESUME_INIT);
		if (boot_from_off)
			hook_notify(HOOK_CHIPSET_RESUME);
#else
		hook_notify(HOOK_CHIPSET_RESUME);
#endif
		sleep_resume_transition();

		boot_from_off = 0;
		disable_sleep(SLEEP_MASK_AP_RUN);
		return POWER_S0;

	case POWER_S0:
		check_for_warm_reset_event();

		shutdown_from_on = check_for_power_off_event();
		if (shutdown_from_on) {
			return POWER_S0S3;
		} else if (power_get_host_sleep_state() ==
				   HOST_SLEEP_EVENT_S3_SUSPEND &&
			   chipset_get_sleep_signal()) {
			return POWER_S0S3;
		}
		/* When receive the host event, trigger the RESUME hook. */
		sleep_notify_transition(SLEEP_NOTIFY_RESUME,
					HOOK_CHIPSET_RESUME);
		break;

	case POWER_S0S3:
		cancel_power_button_timer();

		/*
		 * Call SUSPEND hooks only if we haven't notified listeners of
		 * S3 suspend.
		 */
		sleep_notify_transition(SLEEP_NOTIFY_SUSPEND,
					HOOK_CHIPSET_SUSPEND);
#ifdef CONFIG_CHIPSET_RESUME_INIT_HOOK
		/*
		 * Pair with the HOOK_CHIPSET_RESUME_INIT, i.e. disabling SPI
		 * driver, by notifying the SUSPEND_COMPLETE hooks.
		 *
		 * If shutdown from an on state, notify the SUSPEND hooks too;
		 * otherwise (suspend from S0), the normal SUSPEND hooks have
		 * been notified in the above sleep_notify_transition() call.
		 */
		if (shutdown_from_on)
			hook_notify(HOOK_CHIPSET_SUSPEND);
		hook_notify(HOOK_CHIPSET_SUSPEND_COMPLETE);
#else
		hook_notify(HOOK_CHIPSET_SUSPEND);
#endif
		sleep_suspend_transition();

		enable_sleep(SLEEP_MASK_AP_RUN);
		return POWER_S3;

	case POWER_S3S5:
		cancel_power_button_timer();

		/* Call hooks before we drop power rails */
		hook_notify(HOOK_CHIPSET_SHUTDOWN);

		power_off_seq(shutdown_from_on);
		CPRINTS("power shutdown complete");

		/* Call hooks after we drop power rails */
		hook_notify(HOOK_CHIPSET_SHUTDOWN_COMPLETE);

		shutdown_from_on = 0;

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

test_mockable_static int command_power(int argc, const char **argv)
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
