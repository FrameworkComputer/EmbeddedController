/* Copyright 2018 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* mt8183 chipset power control module for Chrome EC */

#include "builtin/assert.h"
#include "charge_state.h"
#include "chipset.h"
#include "common.h"
#include "console.h"
#include "ec_commands.h"
#include "gpio.h"
#include "hooks.h"
#include "lid_switch.h"
#include "power.h"
#include "power_button.h"
#include "system.h"
#include "task.h"
#include "timer.h"
#include "util.h"

/*
 * mt8183 has two different power sequence versions
 * 0: for normal tablet and detachable form factor
 * 1: for boards have GPIO_EN_PP1800_S5_L
 * CONFIG_CHIPSET_POWER_SEQ_VERSION defaults to 0, re-define the power seq
 * version if needed.
 */

/* Console output macros */
#define CPUTS(outstr) cputs(CC_CHIPSET, outstr)
#define CPRINTS(format, args...) cprints(CC_CHIPSET, format, ##args)

/* Input state flags */
#define IN_PGOOD_PMIC POWER_SIGNAL_MASK(PMIC_PWR_GOOD)
#define IN_SUSPEND_ASSERTED POWER_SIGNAL_MASK(AP_IN_S3_L)

/* Rails required for S3 and S0 */
#define IN_PGOOD_S0 (IN_PGOOD_PMIC)
#define IN_PGOOD_S3 (IN_PGOOD_PMIC)

/* All inputs in the right state for S0 */
#define IN_ALL_S0 (IN_PGOOD_S0 & ~IN_SUSPEND_ASSERTED)

/* Long power key press to force shutdown in S0. go/crosdebug */
#ifdef VARIANT_KUKUI_JACUZZI
#define FORCED_SHUTDOWN_DELAY (8 * SECOND)
#else
#define FORCED_SHUTDOWN_DELAY (10 * SECOND)
#endif

/* Long power key press to boot from S5/G3 state. */
#ifndef POWERBTN_BOOT_DELAY
#define POWERBTN_BOOT_DELAY (1 * SECOND)
#endif

#define CHARGER_INITIALIZED_DELAY_MS 100
#define CHARGER_INITIALIZED_TRIES 40

#define PMIC_EN_PULSE_MS 50

/* Maximum time it should for PMIC to turn on after toggling PMIC_EN_ODL. */
#define PMIC_EN_TIMEOUT (300 * MSEC)

/*
 * Amount of time we need to hold PMIC_FORCE_RESET_ODL to ensure PMIC is really
 * off and will not restart on its own.
 */
#define PMIC_FORCE_RESET_TIME (10 * SECOND)

/* Time delay in G3 to deassert EN_PP1800_S5_L */
#define EN_PP1800_S5_L_DEASSERT_TIME (20 * MSEC)

/* Data structure for a GPIO operation for power sequencing */
struct power_seq_op {
	/* enum gpio_signal in 8 bits */
	uint8_t signal;
	uint8_t level;
	/* Number of milliseconds to delay after setting signal to level */
	uint8_t delay;
};
BUILD_ASSERT(GPIO_COUNT < 256);

/*
 * This is the power sequence for POWER_S5S3.
 * The entries in the table are handled sequentially from the top
 * to the bottom.
 */

static const struct power_seq_op s5s3_power_seq[] = {
	/* Release PMIC watchdog. */
	{ GPIO_PMIC_WATCHDOG_L, 1, 0 },
	/* Turn on AP. */
	{ GPIO_AP_SYS_RST_L, 1, 2 },
};

/* The power sequence for POWER_S3S0 */
static const struct power_seq_op s3s0_power_seq[] = {};

/* The power sequence for POWER_S0S3 */
static const struct power_seq_op s0s3_power_seq[] = {};

/* The power sequence for POWER_S3S5 */
static const struct power_seq_op s3s5_power_seq[] = {
	/* Turn off AP. */
	{ GPIO_AP_SYS_RST_L, 0, 0 },
	/* Assert watchdog to PMIC (there may be a 1.6ms debounce) */
	{ GPIO_PMIC_WATCHDOG_L, 0, 3 },
};

static int forcing_shutdown;
static int boot_from_cutoff;

void chipset_reset_request_interrupt(enum gpio_signal signal)
{
	chipset_reset(CHIPSET_RESET_AP_REQ);
}

/*
 * Triggers on falling edge of AP watchdog line only. The falling edge can
 * happen in these 3 cases:
 *  - AP asserts watchdog while the AP is on: this is a real AP-initiated reset.
 *  - EC asserted GPIO_AP_SYS_RST_L, so the AP is in reset and AP watchdog falls
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
	if (power_get_signals() & IN_PGOOD_PMIC)
		chipset_reset(CHIPSET_RESET_AP_WATCHDOG);
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

/* If chipset needs to be reset, EC also reboots to RO. */
void chipset_reset(enum chipset_shutdown_reason reason)
{
	int flags = SYSTEM_RESET_HARD;

	CPRINTS("%s: %d", __func__, reason);
	report_ap_reset(reason);

	cflush();
	if (reason == CHIPSET_RESET_AP_WATCHDOG)
		flags |= SYSTEM_RESET_AP_WATCHDOG;

	system_reset(flags);

	/* This should not be reachable. */
	while (1)
		;
}

enum power_state power_chipset_init(void)
{
	/* Enable reboot / sleep control inputs from AP */
	gpio_enable_interrupt(GPIO_WARM_RESET_REQ);
	gpio_enable_interrupt(GPIO_AP_IN_SLEEP_L);

	if (system_jumped_to_this_image()) {
		if ((power_get_signals() & IN_ALL_S0) == IN_ALL_S0) {
			disable_sleep(SLEEP_MASK_AP_RUN);
			gpio_enable_interrupt(GPIO_AP_EC_WATCHDOG_L);
			CPRINTS("already in S0");
			return POWER_S0;
		}
	} else if (system_get_reset_flags() & EC_RESET_FLAG_AP_OFF) {
		/* Force shutdown from S5 if the PMIC is already up. */
		if (power_get_signals() & IN_PGOOD_PMIC) {
			forcing_shutdown = 1;
			return POWER_S5;
		}
	} else {
		/* Auto-power on */
		chipset_exit_hard_off();

		if (system_get_reset_flags() == EC_RESET_FLAG_RESET_PIN)
			boot_from_cutoff = 1;
	}

	/* Start from S5 if the PMIC is already up. */
	if (power_get_signals() & IN_PGOOD_PMIC)
		return POWER_S5;

	return POWER_G3;
}

/*
 * If we have to force reset the PMIC, we only need to do so for a few seconds,
 * then we need to release the GPIO to prevent leakage in G3.
 */
static void release_pmic_force_reset(void)
{
	CPRINTS("Releasing PMIC force reset");
	gpio_set_level(GPIO_PMIC_FORCE_RESET_ODL, 1);
}
DECLARE_DEFERRED(release_pmic_force_reset);

/**
 * Step through the power sequence table and do corresponding GPIO operations.
 *
 * @param	power_seq_ops	The pointer to the power sequence table.
 * @param	op_count	The number of entries of power_seq_ops.
 */
static void power_seq_run(const struct power_seq_op *power_seq_ops,
			  int op_count)
{
	int i;

	for (i = 0; i < op_count; i++) {
		gpio_set_level(power_seq_ops[i].signal, power_seq_ops[i].level);
		if (!power_seq_ops[i].delay)
			continue;
		crec_msleep(power_seq_ops[i].delay);
	}
}

#if CONFIG_CHIPSET_POWER_SEQ_VERSION == 1
static void deassert_en_pp1800_s5_l(void)
{
	gpio_set_level(GPIO_EN_PP1800_S5_L, 1);
}
DECLARE_DEFERRED(deassert_en_pp1800_s5_l);
#endif

enum power_state power_handle_state(enum power_state state)
{
	/*
	 * Set if we already had a rising edge on AP_SYS_RST_L. If so, any
	 * subsequent boot attempt will require an EC reset.
	 */
	static int booted;

	/* Retry S5->S3 transition, if not zero. */
	static int s5s3_retry;

	/*
	 * PMIC power went away (AP most likely decided to shut down):
	 * transition to S5, G3.
	 */
	static int ap_shutdown;
	uint16_t tries = 0;

	switch (state) {
	case POWER_G3:

#if CONFIG_CHIPSET_POWER_SEQ_VERSION == 1
		hook_call_deferred(&deassert_en_pp1800_s5_l_data,
				   EN_PP1800_S5_L_DEASSERT_TIME);
#endif

		/* Go back to S5->G3 if the PMIC unexpectedly starts again. */
		if (power_get_signals() & IN_PGOOD_PMIC)
			return POWER_S5G3;
		break;

	case POWER_S5:
		boot_from_cutoff = 0;

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
		 */
		CPRINTS("Forcing shutdown with long press.");
		gpio_set_level(GPIO_PMIC_EN_ODL, 0);

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

#ifdef CONFIG_BATTERY_SMART
		/*
		 * b:148045048: With the adapter to activate the smart battery
		 * which is shutdown mode, will enable PMIC during activation
		 * and have heavy loading, which will prevent the system from
		 * powering on. Delay to boot system until the smart battry
		 * is ready.
		 */
		if (battery_hw_present() && boot_from_cutoff) {
			static int total_sleep_ms;

			if (total_sleep_ms < 4000) {
				crec_msleep(10);
				total_sleep_ms += 10;
				return POWER_G3S5;
			}
		}
#endif

		/*
		 * Allow time for charger to be initialized, in case we're
		 * trying to boot the AP with no battery.
		 */
		while (charge_prevent_power_on(0) &&
		       tries++ < CHARGER_INITIALIZED_TRIES) {
			crec_msleep(CHARGER_INITIALIZED_DELAY_MS);
		}

		/* Return to G3 if battery level is too low. */
		if (charge_want_shutdown() ||
		    tries > CHARGER_INITIALIZED_TRIES) {
			CPRINTS("power-up inhibited");
			chipset_force_shutdown(
				CHIPSET_SHUTDOWN_BATTERY_INHIBIT);
			return POWER_G3;
		}

#if CONFIG_CHIPSET_POWER_SEQ_VERSION == 1
		hook_call_deferred(&deassert_en_pp1800_s5_l_data, -1);
#endif
		hook_call_deferred(&release_pmic_force_reset_data, -1);
		gpio_set_level(GPIO_PMIC_FORCE_RESET_ODL, 1);
#if CONFIG_CHIPSET_POWER_SEQ_VERSION == 1
		gpio_set_level(GPIO_EN_PP1800_S5_L, 0);
#endif

		/* Power up to next state */
		return POWER_S5;

	case POWER_S5S3:
		hook_notify(HOOK_CHIPSET_PRE_INIT);

		/*
		 * Release power button in case it was pressed by force shutdown
		 * sequence.
		 */
		gpio_set_level(GPIO_PMIC_EN_ODL, 1);

		/* If PMIC is off, switch it on by pulsing PMIC enable. */
		if (!(power_get_signals() & IN_PGOOD_PMIC)) {
			crec_msleep(PMIC_EN_PULSE_MS);
			gpio_set_level(GPIO_PMIC_EN_ODL, 0);
			crec_msleep(PMIC_EN_PULSE_MS);
			gpio_set_level(GPIO_PMIC_EN_ODL, 1);
		}

		/* If EC jumped, or has already booted once, reboot to RO. */
		if (system_jumped_to_this_image() || booted) {
			/*
			 * TODO(b:109850749): How quickly does the EC come back
			 * up? Would IN_PGOOD_PMIC be ready by the time we are
			 * back? According to PMIC spec, it should take ~158 ms
			 * after debounce (32 ms), minus PMIC_EN_PULSE_MS above.
			 * It would be good to avoid another _EN pulse above.
			 */
			chipset_reset(CHIPSET_RESET_AP_REQ);
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

		booted = 1;
		/* Enable S3 power supplies, release AP reset. */
		power_seq_run(s5s3_power_seq, ARRAY_SIZE(s5s3_power_seq));
		gpio_enable_interrupt(GPIO_AP_EC_WATCHDOG_L);

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
		power_seq_run(s3s0_power_seq, ARRAY_SIZE(s3s0_power_seq));

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

#ifdef CONFIG_POWER_SLEEP_FAILURE_DETECTION
		sleep_suspend_transition();
#endif /* CONFIG_POWER_SLEEP_FAILURE_DETECTION */

		/*
		 * TODO(b:109850749): Check if we need some delay here to
		 * "debounce" entering suspend (rk3399 uses 20ms delay).
		 */

		power_seq_run(s0s3_power_seq, ARRAY_SIZE(s0s3_power_seq));

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

		gpio_disable_interrupt(GPIO_AP_EC_WATCHDOG_L);
		power_seq_run(s3s5_power_seq, ARRAY_SIZE(s3s5_power_seq));

		/* Call hooks after we remove power rails */
		hook_notify(HOOK_CHIPSET_SHUTDOWN_COMPLETE);

		/* Start shutting down */
		return POWER_S5;

	case POWER_S5G3:
		/* Release the power button, in case it was long pressed. */
		if (forcing_shutdown)
			gpio_set_level(GPIO_PMIC_EN_ODL, 1);

		/*
		 * If PMIC is still not off, assert PMIC_FORCE_RESET_ODL.
		 * This should only happen for forced shutdown where the AP is
		 * not able to send a command to the PMIC, and where the long
		 * power+home press did not work (if the PMIC is misconfigured).
		 * Also, PMIC will lose RTC state, in that case.
		 */
		if (power_get_signals() & IN_PGOOD_PMIC) {
			CPRINTS("Forcing PMIC off");
			gpio_set_level(GPIO_PMIC_FORCE_RESET_ODL, 0);
			crec_msleep(5);
			hook_call_deferred(&release_pmic_force_reset_data,
					   PMIC_FORCE_RESET_TIME);

			return POWER_S5G3;
		}

		return POWER_G3;

	default:
		CPRINTS("Unexpected power state %d", state);
		ASSERT(0);
		break;
	}

	return state;
}

#ifdef CONFIG_POWER_TRACK_HOST_SLEEP_STATE
__override void power_chipset_handle_sleep_hang(enum sleep_hang_type hang_type)
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

#ifdef CONFIG_LID_SWITCH
static void lid_changed(void)
{
	/* Power-up from off on lid open */
	if (lid_is_open() && chipset_in_state(CHIPSET_STATE_ANY_OFF))
		chipset_exit_hard_off();
}
DECLARE_HOOK(HOOK_LID_CHANGE, lid_changed, HOOK_PRIO_DEFAULT);
#endif
