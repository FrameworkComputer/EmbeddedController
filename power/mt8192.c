/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * MT8192 SoC power sequencing module for Chrome EC
 *
 * This implements the following features:
 *
 * - Cold reset powers on the AP
 *
 * When powered off:
 *  - Press power button turns on the AP
 *  - Hold power button turns on the AP, and then 8s later turns it off and
 *    leaves it off until pwron is released and press again.
 *  - Lid open turns on the AP
 *
 *  When powered on:
 *  - Holding power button for 8s powers off the AP
 *  - Pressing and releaseing pwron within that 8s is ignored
 */

#include "battery.h"
#include "builtin/assert.h"
#include "chipset.h"
#include "common.h"
#include "gpio.h"
#include "hooks.h"
#include "lid_switch.h"
#include "power.h"
#include "power_button.h"
#include "system.h"
#include "task.h"
#include "timer.h"
#include "util.h"

#ifdef CONFIG_BRINGUP
#define GPIO_SET_LEVEL(signal, value) \
	gpio_set_level_verbose(CC_CHIPSET, signal, value)
#else
#define GPIO_SET_LEVEL(signal, value) gpio_set_level(signal, value)
#endif

/* Console output macros */
#define CPRINTS(format, args...) cprints(CC_CHIPSET, format, ##args)

/* Input state flags */
#define IN_SUSPEND_ASSERTED POWER_SIGNAL_MASK(AP_IN_S3_L)
#define IN_PGOOD_PMIC POWER_SIGNAL_MASK(PMIC_PWR_GOOD)
#define IN_AP_WDT_ASSERTED POWER_SIGNAL_MASK(AP_WDT_ASSERTED)

/* Rails required for S3 and S0 */
#define IN_PGOOD_S0 (IN_PGOOD_PMIC)
#define IN_PGOOD_S3 (IN_PGOOD_PMIC)

/* All inputs in the right state for S0 */
#define IN_ALL_S0 (IN_PGOOD_S0 & ~IN_SUSPEND_ASSERTED)

/* Long power key press to force shutdown in S0. go/crosdebug */
#define FORCED_SHUTDOWN_DELAY (8 * SECOND)

/* Long power key press to boot from S5/G3 state. */
#ifndef POWERBTN_BOOT_DELAY
#define POWERBTN_BOOT_DELAY (10 * MSEC)
#endif
#define PMIC_EN_PULSE_MS 50

/* Maximum time it should for PMIC to turn on after toggling PMIC_EN_ODL. */
#define PMIC_EN_TIMEOUT (300 * MSEC)

/*
 * Time delay for AP on/off the AP_EC_WDT when received SYS_RST_ODL.
 * Generally it can be done within 3 ms.
 */
#define AP_EC_WDT_TIMEOUT (100 * MSEC)

/* 30 ms for hard reset, we hold it longer to prevent TPM false alarm. */
#define SYS_RST_PULSE_LENGTH (50 * MSEC)

#ifndef CONFIG_ZEPHYR

/* power signal list.  Must match order of enum power_signal. */
const struct power_signal_info power_signal_list[] = {
	{ GPIO_PMIC_EC_PWRGD, POWER_SIGNAL_ACTIVE_HIGH, "PMIC_PWR_GOOD" },
	{ GPIO_AP_IN_SLEEP_L, POWER_SIGNAL_ACTIVE_LOW, "AP_IN_S3_L" },
	{ GPIO_AP_EC_WATCHDOG_L, POWER_SIGNAL_ACTIVE_LOW, "AP_WDT_ASSERTED" },
};
BUILD_ASSERT(ARRAY_SIZE(power_signal_list) == POWER_SIGNAL_COUNT);

#endif /* !CONFIG_ZEPHYR */

static int forcing_shutdown;

static void watchdog_interrupt_deferred(void)
{
	chipset_reset(CHIPSET_RESET_AP_WATCHDOG);
}
DECLARE_DEFERRED(watchdog_interrupt_deferred);

static void reset_request_interrupt_deferred(void)
{
	chipset_reset(CHIPSET_RESET_AP_REQ);
}
DECLARE_DEFERRED(reset_request_interrupt_deferred);

void chipset_reset_request_interrupt(enum gpio_signal signal)
{
	hook_call_deferred(&reset_request_interrupt_deferred_data, 0);
}

/*
 * Triggers on falling edge of AP watchdog line only. The falling edge can
 * happen in these 3 cases:
 *  - AP asserts watchdog while the AP is on: this is a real AP-initiated reset.
 *  - EC asserted GPIO_SYS_RST_ODL, so the AP is in reset and AP watchdog falls
 *    as well. This is _not_ a watchdog reset. We mask these cases by disabling
 *    the interrupt just before shutting down the AP, and re-enabling it just
 *    after starting the AP.
 *  - PMIC has shut down (e.g. the AP powered off by itself), this is not a
 *    watchdog reset either. This should be covered by the case above if the
 *    EC reacts quickly enough, but we mask those cases as well by testing if
 *    the PMIC is still on when the watchdog line falls.
 */
void chipset_watchdog_interrupt(enum gpio_signal signal)
{
	/* Pass AP_EC_WATCHDOG_L signal to PMIC */
	GPIO_SET_LEVEL(GPIO_EC_PMIC_WATCHDOG_L, gpio_get_level(signal));

	/* Update power signals */
	power_signal_interrupt(signal);

	/*
	 * case 1: PMIC is good, WDT asserts, and EC is not asserting
	 * SYS_RST_ODL. This is AP initiated real WDT.
	 */
	if (gpio_get_level(GPIO_SYS_RST_ODL) &&
	    power_get_signals() & IN_PGOOD_PMIC &&
	    power_get_signals() & IN_AP_WDT_ASSERTED)
		hook_call_deferred(&watchdog_interrupt_deferred_data, 0);

	/*
	 * case 2&3: Fall through. The chipset_reset should have been
	 * invoked.
	 */
}

void chipset_force_shutdown(enum chipset_shutdown_reason reason)
{
	CPRINTS("%s(%d)", __func__, reason);
	report_ap_reset(reason);

	/*
	 * Force power off. This condition will reset once the state machine
	 * transitions to G3.
	 */
	forcing_shutdown = 1;
	task_wake(TASK_ID_CHIPSET);
}

void chipset_force_shutdown_button(void)
{
	chipset_force_shutdown(CHIPSET_SHUTDOWN_BUTTON);
}
DECLARE_DEFERRED(chipset_force_shutdown_button);

void chipset_exit_hard_off_button(void)
{
	/* Power up from off */
	forcing_shutdown = 0;
	chipset_exit_hard_off();
}
DECLARE_DEFERRED(chipset_exit_hard_off_button);

void chipset_reset(enum chipset_shutdown_reason reason)
{
	CPRINTS("%s: %d", __func__, reason);
	report_ap_reset(reason);

	GPIO_SET_LEVEL(GPIO_SYS_RST_ODL, 0);
	crec_usleep(SYS_RST_PULSE_LENGTH);
	GPIO_SET_LEVEL(GPIO_SYS_RST_ODL, 1);
}

#ifdef CONFIG_POWER_TRACK_HOST_SLEEP_STATE
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
#endif /* CONFIG_POWER_TRACK_HOST_SLEEP_STATE */

enum power_state power_chipset_init(void)
{
	int exit_hard_off = 1;
	uint32_t reset_flags = system_get_reset_flags();

	/* Enable reboot / sleep control inputs from AP */
	gpio_enable_interrupt(GPIO_AP_EC_WARM_RST_REQ);
	gpio_enable_interrupt(GPIO_AP_IN_SLEEP_L);

	if (system_jumped_late()) {
		if ((power_get_signals() & IN_ALL_S0) == IN_ALL_S0) {
			disable_sleep(SLEEP_MASK_AP_RUN);
			power_signal_enable_interrupt(GPIO_AP_EC_WATCHDOG_L);
			CPRINTS("already in S0");
			return POWER_S0;
		}
	} else if ((reset_flags & EC_RESET_FLAG_AP_OFF) ||
		   (reset_flags & EC_RESET_FLAG_AP_IDLE)) {
		exit_hard_off = 0;
	} else if ((reset_flags & EC_RESET_FLAG_HIBERNATE) &&
		   gpio_get_level(GPIO_AC_PRESENT)) {
		/*
		 * If AC present, assume this is a wake-up by AC insert.
		 * Boot EC only.
		 *
		 * Note that extpower module is not initialized at this point,
		 * the only way is to ask GPIO_AC_PRESENT directly.
		 */
		exit_hard_off = 0;
	}

	if (battery_is_present() == BP_YES)
		/*
		 * (crosbug.com/p/28289): Wait battery stable.
		 * Some batteries use clock stretching feature, which requires
		 * more time to be stable.
		 */
		battery_wait_for_stable();

	if (exit_hard_off)
		/* Auto-power on */
		chipset_exit_hard_off();

	/* Start from S5 if the PMIC is already up. */
	if (power_get_signals() & IN_PGOOD_PMIC) {
		/* Force shutdown from S5 if the PMIC is already up. */
		if (!exit_hard_off)
			forcing_shutdown = 1;
		return POWER_S5;
	}

	return POWER_G3;
}

enum power_state power_handle_state(enum power_state state)
{
	/* Retry S5->S3 transition, if not zero. */
	static int s5s3_retry;

	/*
	 * PMIC power went away (AP most likely decided to shut down):
	 * transition to S5, G3.
	 */
	static int ap_shutdown;

	switch (state) {
	case POWER_G3:

		/* Go back to S5->G3 if the PMIC unexpectedly starts again. */
		if (power_get_signals() & IN_PGOOD_PMIC)
			return POWER_S5G3;
		break;

	case POWER_S5:
		/*
		 * If AP initiated shutdown, PMIC is off, and we can transition
		 * to G3 immediately.
		 */
		if (ap_shutdown) {
			ap_shutdown = 0;
			return POWER_S5G3;
		} else if (!forcing_shutdown) {
			/* Powering up. */
			s5s3_retry = 1;
			return POWER_S5S3;
		}

		/* Forcing shutdown */

		/* Long press has worked, transition to G3. */
		if (!(power_get_signals() & IN_PGOOD_PMIC))
			return POWER_S5G3;

		/*
		 * Try to force PMIC shutdown with a long press. This takes 8s,
		 * shorter than the common code S5->G3 timeout (10s).
		 *
		 * Note: We might run twice at this line because we
		 * deasserts SYS_RST_ODL in S5->S3 and then WDT interrupt
		 * handler sets the wake event for chipset_task. This should be
		 * no harm, but to prevent misunderstanding in the console, we
		 * check EC_PMIC_EN_ODL before set.
		 */
		if (gpio_get_level(GPIO_EC_PMIC_EN_ODL)) {
			CPRINTS("Forcing shutdown with long press.");
			GPIO_SET_LEVEL(GPIO_EC_PMIC_EN_ODL, 0);
		}

		/*
		 * Stay in S5, common code will drop to G3 after timeout
		 * if the long press does not work.
		 */
		return POWER_S5;

	case POWER_S3:
		if (!power_has_signals(IN_PGOOD_S3) || forcing_shutdown)
			return POWER_S3S5;
		else if (!(power_get_signals() & IN_SUSPEND_ASSERTED))
			return POWER_S3S0;
		break;

	case POWER_S0:
		if (!power_has_signals(IN_PGOOD_S0) || forcing_shutdown ||
		    power_get_signals() & IN_SUSPEND_ASSERTED)
			return POWER_S0S3;

		break;

	case POWER_G3S5:
		forcing_shutdown = 0;

		/* Power up to next state */
		return POWER_S5;

	case POWER_S5S3:
		hook_notify(HOOK_CHIPSET_PRE_INIT);

		/*
		 * Release power button in case it was pressed by force shutdown
		 * sequence.
		 */
		GPIO_SET_LEVEL(GPIO_EC_PMIC_EN_ODL, 1);

		/* If PMIC is off, switch it on by pulsing PMIC enable. */
		if (!(power_get_signals() & IN_PGOOD_PMIC)) {
			crec_msleep(PMIC_EN_PULSE_MS);
			GPIO_SET_LEVEL(GPIO_EC_PMIC_EN_ODL, 0);
			crec_msleep(PMIC_EN_PULSE_MS);
			GPIO_SET_LEVEL(GPIO_EC_PMIC_EN_ODL, 1);
		}

		/*
		 * Wait for PMIC to bring up rails. Retry if it fails
		 * (it may take 2 attempts on restart after we use
		 * force reset).
		 */
		if (power_wait_signals_timeout(IN_PGOOD_PMIC,
					       PMIC_EN_TIMEOUT)) {
			if (s5s3_retry) {
				s5s3_retry = 0;
				return POWER_S5S3;
			}
			/* Give up, go back to G3. */
			return POWER_S5G3;
		}

		/* Release AP reset and waits for AP pulling WDT up. */
		power_signal_enable_interrupt(GPIO_AP_EC_WATCHDOG_L);
		GPIO_SET_LEVEL(GPIO_SYS_RST_ODL, 1);
		if (power_wait_mask_signals_timeout(0, IN_AP_WDT_ASSERTED,
						    AP_EC_WDT_TIMEOUT)) {
			if (s5s3_retry) {
				s5s3_retry = 0;
				return POWER_S5S3;
			}
			/* Give up, go back to G3. */
			return POWER_S5G3;
		}

		/* Call hooks now that rails are up */
		hook_notify(HOOK_CHIPSET_STARTUP);

		/*
		 * Clearing the sleep failure detection tracking on the path
		 * to S0 to handle any reset conditions.
		 */
#ifdef CONFIG_POWER_SLEEP_FAILURE_DETECTION
		power_reset_host_sleep_state();
#endif /* CONFIG_POWER_SLEEP_FAILURE_DETECTION */
		/* Power up to next state */
		return POWER_S3;

	case POWER_S3S0:
#ifdef CONFIG_CHIPSET_RESUME_INIT_HOOK
		/* Call hooks prior to chipset resume */
		hook_notify(HOOK_CHIPSET_RESUME_INIT);
#endif

		if (power_wait_signals(IN_PGOOD_S0)) {
			chipset_force_shutdown(CHIPSET_SHUTDOWN_WAIT);
			return POWER_S0S3;
		}

		/* Call hooks now that rails are up */
		hook_notify(HOOK_CHIPSET_RESUME);

#ifdef CONFIG_POWER_SLEEP_FAILURE_DETECTION
		sleep_resume_transition();
#endif /* CONFIG_POWER_SLEEP_FAILURE_DETECTION */

		/*
		 * Disable idle task deep sleep. This means that the low
		 * power idle task will not go into deep sleep while in S0.
		 */
		disable_sleep(SLEEP_MASK_AP_RUN);

		/* Power up to next state */
		return POWER_S0;

	case POWER_S0S3:
		/* Call hooks before we remove power rails */
		hook_notify(HOOK_CHIPSET_SUSPEND);
#ifdef CONFIG_CHIPSET_RESUME_INIT_HOOK
		/* Call hooks after chipset suspend */
		hook_notify(HOOK_CHIPSET_SUSPEND_COMPLETE);
#endif

#ifdef CONFIG_POWER_SLEEP_FAILURE_DETECTION
		sleep_suspend_transition();
#endif /* CONFIG_POWER_SLEEP_FAILURE_DETECTION */

		/*
		 * Enable idle task deep sleep. Allow the low power idle task
		 * to go into deep sleep in S3 or lower.
		 */
		enable_sleep(SLEEP_MASK_AP_RUN);

		/*
		 * In case the power button is held awaiting power-off timeout,
		 * power off immediately now that we're entering S3.
		 */
		if (power_button_is_pressed()) {
			forcing_shutdown = 1;
			hook_call_deferred(&chipset_force_shutdown_button_data,
					   -1);
		}

		return POWER_S3;

	case POWER_S3S5:
		/* PMIC has shutdown, transition to G3. */
		if (!(power_get_signals() & IN_PGOOD_PMIC))
			ap_shutdown = 1;

		/* Call hooks before we remove power rails */
		hook_notify(HOOK_CHIPSET_SHUTDOWN);

		/*
		 * Assert SYS_RST_ODL, and waits for AP finishing epilogue and
		 * asserting WDT.
		 */
		GPIO_SET_LEVEL(GPIO_SYS_RST_ODL, 0);
		if (EC_ERROR_TIMEOUT ==
		    power_wait_signals_timeout(IN_AP_WDT_ASSERTED,
					       AP_EC_WDT_TIMEOUT)) {
			CPRINTS("Timeout waitting AP watchdog, force if off");
			GPIO_SET_LEVEL(GPIO_EC_PMIC_WATCHDOG_L, 0);
		}
		power_signal_disable_interrupt(GPIO_AP_EC_WATCHDOG_L);

		/* Call hooks after we remove power rails */
		hook_notify(HOOK_CHIPSET_SHUTDOWN_COMPLETE);

		/* Start shutting down */
		return POWER_S5;

	case POWER_S5G3:
		/* Release the power button, in case it was long pressed. */
		if (forcing_shutdown)
			GPIO_SET_LEVEL(GPIO_EC_PMIC_EN_ODL, 1);

		/* If PMIC is not off, go back to S5 and try again. */
		if (power_get_signals() & IN_PGOOD_PMIC)
			return POWER_S5;

		return POWER_G3;

	default:
		CPRINTS("Unexpected power state %d", state);
		ASSERT(0);
		break;
	}

	return state;
}

static void power_button_changed(void)
{
	if (power_button_is_pressed()) {
		if (chipset_in_state(CHIPSET_STATE_ANY_OFF))
			hook_call_deferred(&chipset_exit_hard_off_button_data,
					   POWERBTN_BOOT_DELAY);

		/* Delayed power down from S0/S3, cancel on PB release */
		hook_call_deferred(&chipset_force_shutdown_button_data,
				   FORCED_SHUTDOWN_DELAY);
	} else {
		/* Power button released, cancel deferred shutdown/boot */
		hook_call_deferred(&chipset_exit_hard_off_button_data, -1);
		hook_call_deferred(&chipset_force_shutdown_button_data, -1);
	}
}
DECLARE_HOOK(HOOK_POWER_BUTTON_CHANGE, power_button_changed, HOOK_PRIO_DEFAULT);

#ifdef CONFIG_POWER_TRACK_HOST_SLEEP_STATE
__overridable void
power_chipset_handle_sleep_hang(enum sleep_hang_type hang_type)
{
	CPRINTS("Warning: Detected sleep hang! Waking host up!");
	host_set_single_event(EC_HOST_EVENT_HANG_DETECT);
}

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

	} else if (state == HOST_SLEEP_EVENT_S3_RESUME) {
		/*
		 * Wake up chipset task and indicate to power state machine that
		 * listeners need to be notified of chipset resume.
		 */
		sleep_set_notify(SLEEP_NOTIFY_RESUME);
		task_wake(TASK_ID_CHIPSET);
		sleep_complete_resume(ctx);
	}
}
#endif /* CONFIG_POWER_TRACK_HOST_SLEEP_STATE */

#ifdef CONFIG_LID_SWITCH
static void lid_changed(void)
{
	/* Power-up from off on lid open */
	if (lid_is_open() && chipset_in_state(CHIPSET_STATE_ANY_OFF))
		chipset_exit_hard_off();
}
DECLARE_HOOK(HOOK_LID_CHANGE, lid_changed, HOOK_PRIO_DEFAULT);
#endif
