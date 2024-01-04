/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * MT8186/MT8188 SoC power sequencing module for Chrome EC
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
 *  - Pressing and releaseing power within that 8s is ignored
 */

#include "battery.h"
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

#ifdef CONFIG_BRINGUP
#define GPIO_SET_LEVEL(signal, value) \
	gpio_set_level_verbose(CC_CHIPSET, signal, value)
#else
#define GPIO_SET_LEVEL(signal, value) gpio_set_level(signal, value)
#endif

/* Console output macros */
#define CPRINTS(format, args...) cprints(CC_CHIPSET, format, ##args)

/* Input state flags */
#define IN_SUSPEND_ASSERTED POWER_SIGNAL_MASK(AP_IN_S3)
#define IN_AP_RST POWER_SIGNAL_MASK(AP_IN_RST)
#define IN_PG_PP4200_S5 POWER_SIGNAL_MASK(PG_PP4200_S5)
#define IN_PMIC_AP_RST POWER_SIGNAL_MASK(PMIC_AP_RST)

/* Long power key press to force shutdown in S0. go/crosdebug */
#define FORCED_SHUTDOWN_DELAY (8 * SECOND)

/* PG4200 S5 ready delay */
#define PG_PP4200_S5_DELAY (100 * MSEC)

/* Maximum time it should for PMIC to turn on after toggling PMIC_EN_ODL. */
#define PMIC_EN_TIMEOUT (300 * MSEC)
#define PMIC_EN_PULSE_MS 50
/* PMIC hard off delay with 20% tolerance. */
#define PMIC_HARD_OFF_DELAY (8 * SECOND / 100 * 120)
/* Timeout for PMIC resetting AP after hard off. */
#define PMIC_AP_RESET_TIMEOUT (1 * SECOND)

/* 30 ms for hard reset, we hold it longer to prevent TPM false alarm. */
#define SYS_RST_PULSE_LENGTH (50 * MSEC)

/*
 * A delay for distinguish a WDT reset or a normal shutdown. It usually takes
 * 90ms to pull AP_IN_SLEEP_L low in a normal shutdown.
 */
#define NORMAL_SHUTDOWN_DELAY (150 * MSEC)
#define RESET_FLAG_TIMEOUT (2 * SECOND)

#if defined(CONFIG_PLATFORM_EC_POWERSEQ_MT8188) && \
	!DT_NODE_EXISTS(DT_NODELABEL(en_pp4200_s5))
#error Must have dt node en_pp4200_s5 for MT8188 power sequence
#endif

/* indicate MT8186 is processing a chipset reset. */
static bool is_resetting;
/* indicate MT8186 is AP reset is held by servo or GSC. */
test_export_static bool is_held;
/* indicate MT8186 is processing a AP forcing shutdown. */
static bool is_shutdown;
/* indicate MT8186 has been dropped to S5G3 from the last IN_AP_RST state . */
static bool is_s5g3_passed;
/*
 * indicate exiting off state, and don't respect the power signals until chipset
 * on.
 */
static bool is_exiting_off;

/* forward declaration */
static enum power_state power_get_signal_state(void);

/* Turn on the PMIC power source to AP, this also boots AP. */
static void set_pmic_pwron(void)
{
	GPIO_SET_LEVEL(GPIO_EC_PMIC_EN_ODL, 1);
	msleep(PMIC_EN_PULSE_MS);
	GPIO_SET_LEVEL(GPIO_EC_PMIC_EN_ODL, 0);
	msleep(PMIC_EN_PULSE_MS);
	GPIO_SET_LEVEL(GPIO_EC_PMIC_EN_ODL, 1);
}

/* Turn off the PMIC power source to AP (forcely), this could take up to 8
 * seconds
 */
static void set_pmic_pwroff(void)
{
	timestamp_t pmic_off_timeout;

	/* We don't have a PMIC PG signal, so we can only blindly assert
	 * the PMIC EN for a long delay time that the spec requiring.
	 */
	GPIO_SET_LEVEL(GPIO_EC_PMIC_EN_ODL, 0);

	pmic_off_timeout = get_time();
	pmic_off_timeout.val += PMIC_HARD_OFF_DELAY;

	while (!timestamp_expired(pmic_off_timeout, NULL)) {
		msleep(100);
	};

	GPIO_SET_LEVEL(GPIO_EC_PMIC_EN_ODL, 1);
}

void chipset_warm_reset_interrupt(enum gpio_signal signal)
{
	/* If this is not a chipset_reset, the ap_rst must be held by gsc or
	 * servo.
	 */
	if (!is_resetting && !gpio_get_level(GPIO_SYS_RST_ODL) &&
	    !gpio_get_level(signal))
		is_held = true;
	else
		is_held = false;

	power_signal_interrupt(signal);
}

static void reset_request_interrupt_deferred(void)
{
	chipset_reset(CHIPSET_RESET_AP_REQ);
}
DECLARE_DEFERRED(reset_request_interrupt_deferred);

void chipset_reset_request_interrupt(enum gpio_signal signal)
{
	power_signal_interrupt(signal);
	hook_call_deferred(&reset_request_interrupt_deferred_data, 0);
}

static void watchdog_interrupt_deferred(void)
{
	/* If it's a real WDT, it must be in S0. */
	if (!(power_get_signals() & (IN_AP_RST | IN_SUSPEND_ASSERTED)))
		chipset_reset(CHIPSET_RESET_AP_WATCHDOG);
}
DECLARE_DEFERRED(watchdog_interrupt_deferred);

void chipset_watchdog_interrupt(enum gpio_signal signal)
{
	power_signal_interrupt(signal);

	/*
	 * We need this guard in that:
	 * 1. AP_EC_WDTRST_L will recursively toggle until the AP is reset.
	 * 2. If a warm reset request or AP shutdown is processing, then this
	 *    interrupt tirgger is a fake WDT interrupt, we should skip it.
	 */
	if (!is_resetting && !is_shutdown)
		hook_call_deferred(&watchdog_interrupt_deferred_data,
				   NORMAL_SHUTDOWN_DELAY);
}

void chipset_force_shutdown(enum chipset_shutdown_reason reason)
{
	int state = power_get_signal_state();
	/* use the signal state instead of the chipset_in_state because the
	 * power_state is not initaialized when chipset_force_shutdown is called
	 * from power_chipset_init.
	 */
	bool chipset_off = (state != POWER_S0 && state != POWER_S3);

	CPRINTS("%s: 0x%x%s", __func__, reason, chipset_off ? "(skipped)" : "");

	if (chipset_off)
		return;

	report_ap_reset(reason);

	is_shutdown = true;

	task_wake(TASK_ID_CHIPSET);
}

void chipset_force_shutdown_button(void)
{
	chipset_force_shutdown(CHIPSET_SHUTDOWN_BUTTON);
}
DECLARE_DEFERRED(chipset_force_shutdown_button);

static void mt8186_exit_off(void)
{
	is_exiting_off = true;
	chipset_exit_hard_off();
}

static void reset_flag_deferred(void)
{
	if (!is_resetting)
		return;

	CPRINTS("chipset_reset failed");
	is_resetting = false;
	task_wake(TASK_ID_CHIPSET);
}
DECLARE_DEFERRED(reset_flag_deferred);

void chipset_reset(enum chipset_shutdown_reason reason)
{
	CPRINTS("%s: 0x%x", __func__, reason);
	report_ap_reset(reason);

	is_resetting = true;
	hook_call_deferred(&reset_flag_deferred_data, RESET_FLAG_TIMEOUT);
	GPIO_SET_LEVEL(GPIO_SYS_RST_ODL, 0);
	usleep(SYS_RST_PULSE_LENGTH);
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
#endif /* CONFIG_POWER_TRACK_HOST_SLEEP_STATE */

/*
 * Power state is determined from the following table:
 *
 *     | IN_AP_RST | IN_SUSPEND_ASSERTED | is_s5g3_passed |
 * --------------------------------------------------------
 *  S0 |         0 |                   0 |               x|
 *  S3 |         0 |                   1 |               x|
 *  S5 |         1 |                   x |               0|
 *  G3 |         1 |                   x |               1|
 *
 * S5 is a temp stage, which will be put into G3 after s5_inactivity_timeout.
 * is_resetting flag indicates it's resetting chipset, and always S0.
 * is_held flag indicates the AP reset is held by servo or GSC, and always S0.
 * is_shutdown flag indicates it's shutting down the AP, it goes for S5.
 * is_s5g3_passed flag indicates it has shutdown from S5 to G3 since last
 * shutdown.
 */
static enum power_state power_get_signal_state(void)
{
	if (is_shutdown)
		return POWER_S5;
	/*
	 * - We are processing a chipset reset(S0->S0), so we don't check the
	 * power signals until the reset is finished. This is because
	 * while the chipset is resetting, the intermediate power signal state
	 * is not reflecting the current power state.
	 *
	 * - GSC or Servo is holding the SYS_RST, in this case, we should stay
	 * at S0.
	 */
	if (is_resetting || is_held)
		return POWER_S0;
	if (power_get_signals() & IN_AP_RST) {
		/* If it has been put to G3 from S5 idle, then stay at G3.*/
		if (is_s5g3_passed)
			return POWER_G3;
		return POWER_S5;
	}
	if (power_get_signals() & IN_SUSPEND_ASSERTED)
		return POWER_S3;
	return POWER_S0;
}

enum power_state power_chipset_init(void)
{
	int exit_hard_off = 1;
	enum power_state init_state = power_get_signal_state();

	if (system_get_reset_flags() & EC_RESET_FLAG_AP_IDLE) {
		if (init_state == POWER_S0) {
			disable_sleep(SLEEP_MASK_AP_RUN);
		}

		return init_state;
	}

	if (system_jumped_late()) {
		if (init_state == POWER_S0) {
			disable_sleep(SLEEP_MASK_AP_RUN);
			CPRINTS("already in S0");
		}
	} else if (system_get_reset_flags() & EC_RESET_FLAG_AP_OFF) {
		exit_hard_off = 0;
	} else if ((system_get_reset_flags() & EC_RESET_FLAG_HIBERNATE) &&
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

	/* If the init signal state is at S5, assigns it to G3 to match the
	 * default GPIO and PP4200_S5 rail states.
	 */
	if (init_state == POWER_S5) {
		init_state = POWER_G3;
		is_s5g3_passed = true;
	}

	if (battery_is_present() == BP_YES)
		/*
		 * (crosbug.com/p/28289): Wait battery stable.
		 * Some batteries use clock stretching feature, which requires
		 * more time to be stable.
		 */
		battery_wait_for_stable();

	if (exit_hard_off && init_state == POWER_G3)
		/* Auto-power on */
		mt8186_exit_off();

	if (init_state != POWER_G3 && !exit_hard_off) {
		/* Force shutdown from S5 if the PMIC is already up. */
		chipset_force_shutdown(CHIPSET_SHUTDOWN_INIT);
	}

	return init_state;
}

enum power_state power_handle_state(enum power_state state)
{
	enum power_state next_state = power_get_signal_state();

	switch (state) {
	case POWER_G3:
		if (next_state != POWER_G3)
			return POWER_G3S5;
		break;

	case POWER_S5:
		if (is_exiting_off)
			return POWER_S5S3;
		else if (next_state == POWER_G3)
			return POWER_S5G3;
		else if (next_state == POWER_S5)
			return POWER_S5;
		else
			return POWER_S5S3;

	case POWER_S3:
		if (next_state == POWER_G3 || next_state == POWER_S5)
			return POWER_S3S5;
		else if (next_state == POWER_S0)
			return POWER_S3S0;
		break;

	case POWER_S0:
		if (next_state != POWER_S0)
			return POWER_S0S3;
		is_resetting = false;

		break;

	case POWER_G3S5:
#if CONFIG_CHARGER_MIN_BAT_PCT_FOR_POWER_ON
		if (!system_can_boot_ap())
			return POWER_G3;
#endif

#if DT_NODE_EXISTS(DT_NODELABEL(en_pp4200_s5))
		power_signal_enable_interrupt(GPIO_PMIC_EC_RESETB);
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(en_pp4200_s5), 1);
		if (power_wait_mask_signals_timeout(IN_PG_PP4200_S5,
						    IN_PG_PP4200_S5,
						    PG_PP4200_S5_DELAY))
			return POWER_S5G3;
#endif

		return POWER_S5;

	case POWER_S5S3:
		/* Off state exited. */
		is_exiting_off = false;
		is_s5g3_passed = false;
		hook_notify(HOOK_CHIPSET_PRE_INIT);

		power_signal_enable_interrupt(GPIO_AP_IN_SLEEP_L);
		power_signal_enable_interrupt(GPIO_AP_EC_WDTRST_L);
		power_signal_enable_interrupt(GPIO_AP_EC_WARM_RST_REQ);

		set_pmic_pwron();

		GPIO_SET_LEVEL(GPIO_SYS_RST_ODL, 1);

		if (power_wait_mask_signals_timeout(0, IN_AP_RST,
						    PMIC_EN_TIMEOUT)) {
			/* Give up, go back to G3. */
			is_shutdown = true;
			return POWER_S3S5;
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
		hook_notify(HOOK_CHIPSET_RESUME_INIT);

		if (power_wait_mask_signals_timeout(0, IN_AP_RST, SECOND)) {
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
		 * Enable idle task deep sleep. Allow the low power idle task
		 * to go into deep sleep in S3 or lower.
		 */
		enable_sleep(SLEEP_MASK_AP_RUN);

		hook_notify(HOOK_CHIPSET_SUSPEND_COMPLETE);

		return POWER_S3;

	case POWER_S3S5:
		/* Stop the power key shutdown deferred in case the power key
		 * is still pressed.
		 */
		hook_call_deferred(&chipset_force_shutdown_button_data, -1);

		power_signal_disable_interrupt(GPIO_AP_IN_SLEEP_L);
		power_signal_disable_interrupt(GPIO_AP_EC_WDTRST_L);
		power_signal_disable_interrupt(GPIO_AP_EC_WARM_RST_REQ);

		/* Only actively reset AP with hard shutdown.
		 * For AP initiated shutdown, the AP has been reset by PMIC.
		 * For servo, gsc initiaed warm reset, EC doesn't need to hold
		 * it.
		 */
		if (is_shutdown)
			GPIO_SET_LEVEL(GPIO_SYS_RST_ODL, 0);

		/* Call hooks before we remove power rails */
		hook_notify(HOOK_CHIPSET_SHUTDOWN);

		/* If this is a forcing shutdown, power off the PMIC now,
		 * and can wait at most up to 8 seconds.
		 */
		if (is_shutdown)
			set_pmic_pwroff();

		hook_notify(HOOK_CHIPSET_SHUTDOWN_COMPLETE);

		is_shutdown = false;
		/* AP down and PMIC off, the servo and GSC is unable to hold the
		 * AP for S0.
		 */
		is_held = false;
		return POWER_S5;

	case POWER_S5G3:
		is_s5g3_passed = true;
#if DT_NODE_EXISTS(DT_NODELABEL(en_pp4200_s5))
		if (power_wait_mask_signals_timeout(IN_PMIC_AP_RST,
						    IN_PMIC_AP_RST,
						    PMIC_AP_RESET_TIMEOUT))
			CPRINTS("PMIC reset AP timeout. Forcing PMIC off");
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(en_pp4200_s5), 0);
		power_signal_disable_interrupt(GPIO_PMIC_EC_RESETB);
#endif
		return POWER_G3;
	default:
		CPRINTS("Unexpected power state %d", state);
		break;
	}

	return state;
}

static void power_button_changed(void)
{
	if (power_button_is_pressed()) {
		if (chipset_in_state(CHIPSET_STATE_ANY_OFF))
			mt8186_exit_off();
		/* Delayed power down from S0/S3, cancel on PB release */
		hook_call_deferred(&chipset_force_shutdown_button_data,
				   FORCED_SHUTDOWN_DELAY);
	} else {
		hook_call_deferred(&chipset_force_shutdown_button_data, -1);
	}
}
DECLARE_HOOK(HOOK_POWER_BUTTON_CHANGE, power_button_changed, HOOK_PRIO_DEFAULT);

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

#ifdef CONFIG_LID_SWITCH
static void lid_changed(void)
{
	/* Power-up from off on lid open */
	if (lid_is_open() && chipset_in_state(CHIPSET_STATE_ANY_OFF))
		mt8186_exit_off();
}
DECLARE_HOOK(HOOK_LID_CHANGE, lid_changed, HOOK_PRIO_DEFAULT);
#endif
