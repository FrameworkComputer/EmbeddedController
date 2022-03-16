/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * MT8186 SoC power sequencing module for Chrome EC
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

#include "assert.h"
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

/* Long power key press to force shutdown in S0. go/crosdebug */
#define FORCED_SHUTDOWN_DELAY (8 * SECOND)

/* Long power key press to boot from S5/G3 state. */
#define POWERBTN_BOOT_DELAY (10 * MSEC)
#define PMIC_EN_PULSE_MS 50

/* Maximum time it should for PMIC to turn on after toggling PMIC_EN_ODL. */
#define PMIC_EN_TIMEOUT (300 * MSEC)

/* 30 ms for hard reset, we hold it longer to prevent TPM false alarm. */
#define SYS_RST_PULSE_LENGTH (50 * MSEC)

/*
 * A delay for distinguish a WDT reset or a normal shutdown. It usually takes
 * 90ms to pull AP_IN_SLEEP_L low in a normal shutdown.
 */
#define NORMAL_SHUTDOWN_DELAY (150 * MSEC)
#define RESET_FLAG_TIMEOUT (2 * SECOND)

#ifndef CONFIG_ZEPHYR
/* power signal list.  Must match order of enum power_signal. */
const struct power_signal_info power_signal_list[] = {
	{GPIO_AP_EC_SYSRST_ODL, POWER_SIGNAL_ACTIVE_LOW, "AP_IN_RST"},
	{GPIO_AP_IN_SLEEP_L, POWER_SIGNAL_ACTIVE_LOW, "AP_IN_S3"},
	{GPIO_AP_EC_WDTRST_L, POWER_SIGNAL_ACTIVE_LOW, "AP_WDT_ASSERTED"},
	{GPIO_AP_EC_WARM_RST_REQ, POWER_SIGNAL_ACTIVE_HIGH, "AP_WARM_RST_REQ"},
};
BUILD_ASSERT(ARRAY_SIZE(power_signal_list) == POWER_SIGNAL_COUNT);
#endif /* CONFIG_ZEPHYR */

/* indicate MT8186 is processing a chipset reset. */
static bool is_resetting;
/* indicate MT8186 is processing a AP shutdown. */
static bool is_shutdown;

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
	/*
	 * If this is a real WDT, AP_IN_SLEEP_L should keep high after
	 * the WDT interrupt is fired. Otherwise, it's a normal shutdown.
	 */
	if (gpio_get_level(GPIO_AP_IN_SLEEP_L))
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

static void release_power_button(void)
{
	CPRINTS("release power button after 8 seconds.");
	GPIO_SET_LEVEL(GPIO_EC_PMIC_EN_ODL, 1);
}
DECLARE_DEFERRED(release_power_button);

void chipset_force_shutdown(enum chipset_shutdown_reason reason)
{
	CPRINTS("%s(%d)", __func__, reason);
	report_ap_reset(reason);

	is_shutdown = true;
	/*
	 * Force power off. This condition will reset once the state machine
	 * transitions to G3.
	 */
	GPIO_SET_LEVEL(GPIO_SYS_RST_ODL, 0);
	CPRINTS("Forcing pmic off with long press.");
	GPIO_SET_LEVEL(GPIO_EC_PMIC_EN_ODL, 0);
	hook_call_deferred(&release_power_button_data,
			   FORCED_SHUTDOWN_DELAY + SECOND);

	task_wake(TASK_ID_CHIPSET);
}

void chipset_force_shutdown_button(void)
{
	chipset_force_shutdown(CHIPSET_SHUTDOWN_BUTTON);
}
DECLARE_DEFERRED(chipset_force_shutdown_button);

void chipset_exit_hard_off_button(void)
{
	/*
	 * release power button in case we are in the 8 seconds long hold
	 * period
	 */
	hook_call_deferred(&release_power_button_data, -1);
	release_power_button();
	/* Power up from off */
	chipset_exit_hard_off();
}
DECLARE_DEFERRED(chipset_exit_hard_off_button);

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
	CPRINTS("%s: %d", __func__, reason);
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

/*
 * Power state is determined from the following table:
 *
 *     | IN_AP_RST | IN_SUSPEND_ASSERTED |
 * ----------------------------------------------
 *  S0 |         0 |                   0 |
 *  S3 |         0 |                   1 |
 *  G3 |         1 |                   x |
 *
 * S5 is only used when exit from G3 in power_common_state().
 * is_resetting flag indicate it's resetting chipset, and it's always S0.
 * is_shutdown flag indicates it's shutting down the AP, it goes for G3.
 */
static enum power_state power_get_signal_state(void)
{
	/*
	 * We are processing a chipset reset(S0->S0), so we don't check the
	 * power signals until the reset is finished. This is because
	 * while the chipset is resetting, the intermediate power signal state
	 * is not reflecting the current power state.
	 */
	if (is_resetting)
		return POWER_S0;
	if (is_shutdown)
		return POWER_G3;
	if (power_get_signals() & IN_AP_RST)
		return POWER_G3;
	if (power_get_signals() & IN_SUSPEND_ASSERTED)
		return POWER_S3;
	return POWER_S0;
}

enum power_state power_chipset_init(void)
{
	int exit_hard_off = 1;
	enum power_state init_state = power_get_signal_state();

	/* Enable reboot / sleep control inputs from AP */
	gpio_enable_interrupt(GPIO_AP_IN_SLEEP_L);
	gpio_enable_interrupt(GPIO_AP_EC_SYSRST_ODL);

	if (system_jumped_late()) {
		if (init_state == POWER_S0) {
			gpio_enable_interrupt(GPIO_AP_EC_WDTRST_L);
			gpio_enable_interrupt(GPIO_AP_EC_WARM_RST_REQ);
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

	if (init_state != POWER_G3 && !exit_hard_off)
		/* Force shutdown from S5 if the PMIC is already up. */
		chipset_force_shutdown(CHIPSET_SHUTDOWN_INIT);

	return init_state;
}

enum power_state power_handle_state(enum power_state state)
{
	enum power_state next_state = power_get_signal_state();

	switch (state) {
	case POWER_G3:
		is_shutdown = false;
		if (next_state != POWER_G3)
			return POWER_G3S5;
		break;

	case POWER_S5:
		return POWER_S5S3;

	case POWER_S3:
		if (next_state == POWER_G3)
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
		return POWER_S5;

	case POWER_S5S3:
		hook_notify(HOOK_CHIPSET_PRE_INIT);

		gpio_enable_interrupt(GPIO_AP_EC_WARM_RST_REQ);
		gpio_enable_interrupt(GPIO_AP_EC_WDTRST_L);

		GPIO_SET_LEVEL(GPIO_SYS_RST_ODL, 1);
		msleep(PMIC_EN_PULSE_MS);
		GPIO_SET_LEVEL(GPIO_EC_PMIC_EN_ODL, 0);
		msleep(PMIC_EN_PULSE_MS);
		GPIO_SET_LEVEL(GPIO_EC_PMIC_EN_ODL, 1);

		if (power_wait_mask_signals_timeout(0, IN_AP_RST,
						    PMIC_EN_TIMEOUT))
			/* Give up, go back to G3. */
			return POWER_S5G3;

		msleep(500);
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

		/*
		 * In case the power button is held awaiting power-off timeout,
		 * power off immediately now that we're entering S3.
		 */
		if (power_button_is_pressed())
			hook_call_deferred(&chipset_force_shutdown_button_data,
					   -1);

		return POWER_S3;

	case POWER_S3S5:
		gpio_disable_interrupt(GPIO_AP_EC_WDTRST_L);
		gpio_disable_interrupt(GPIO_AP_EC_WARM_RST_REQ);

		/* Call hooks before we remove power rails */
		hook_notify(HOOK_CHIPSET_SHUTDOWN);
		hook_notify(HOOK_CHIPSET_SHUTDOWN_COMPLETE);

		/* Skip S5 */
		return POWER_S5G3;

	case POWER_S5G3:
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
__override void power_chipset_handle_sleep_hang(
		enum sleep_hang_type hang_type)
{
	CPRINTS("Warning: Detected sleep hang! Waking host up!");
	host_set_single_event(EC_HOST_EVENT_HANG_DETECT);
}

__override void power_chipset_handle_host_sleep_event(
		enum host_sleep_event state,
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
