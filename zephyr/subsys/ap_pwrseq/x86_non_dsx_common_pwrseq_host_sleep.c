/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/logging/log.h>

#include <ap_power_host_sleep.h>
#include <x86_non_dsx_common_pwrseq_sm_handler.h>

LOG_MODULE_DECLARE(ap_pwrseq, CONFIG_AP_PWRSEQ_LOG_LEVEL);

#ifdef CONFIG_AP_SLP_S0_DEBUG
#include "util.h"

static void slp_s0_debug_alarm(struct k_work *work)
{
	/* Wake up host by rtc event */
	host_set_single_event(EC_HOST_EVENT_RTC);
}
static K_WORK_DELAYABLE_DEFINE(slp_s0_debug_alarm_data, slp_s0_debug_alarm);

static enum ec_status
host_command_slp_s0_debug_alarm(struct host_cmd_handler_args *args)
{
	const struct ec_params_set_alarm_slp_s0_dbg *p = args->params;
	struct k_work_sync work_sync;

	if (p->time < 1)
		k_work_cancel_delayable_sync(&slp_s0_debug_alarm_data,
					     &work_sync);
	else
		k_work_schedule(&slp_s0_debug_alarm_data, K_SECONDS(p->time));

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_SET_ALARM_SLP_S0_DBG,
		     host_command_slp_s0_debug_alarm, EC_VER_MASK(0));

/**
 * Test the RTC alarm by setting an interrupt on RTC match.
 */
static int console_command_slp_s0_debug_alarm(const struct shell *sh, int argc,
					      const char **argv)
{
	uint16_t s;
	char *e;
	struct k_work_sync work_sync;

	s = strtoi(argv[1], &e, 10);
	if (*e) {
		shell_error(sh, "Invalid argument, numbers only");
		return -EINVAL;
	}

	if (s < 1) {
		k_work_cancel_delayable_sync(&slp_s0_debug_alarm_data,
					     &work_sync);
		shell_fprintf(sh, SHELL_INFO,
			      "SLP_S0 debug alarm is canceled\n");
	} else {
		k_work_schedule(&slp_s0_debug_alarm_data, K_SECONDS(s));
		shell_fprintf(sh, SHELL_INFO,
			      "SLP_S0 debug alarm is set to go off in %d sec\n",
			      s);
	}

	return EC_SUCCESS;
}
SHELL_CMD_ARG_REGISTER(slp_s0_debug_alarm, NULL,
		       "Set SLP_S0 alarm time. "
		       "Usage: slp_s0_debug_alarm <seconds>",
		       console_command_slp_s0_debug_alarm, 2, 0);

#endif

/**
 * Type of sleep hang detected
 */
enum sleep_hang_type {
	SLEEP_HANG_NONE,
	SLEEP_HANG_S0IX_SUSPEND,
	SLEEP_HANG_S0IX_RESUME
};

static uint16_t sleep_signal_timeout;
static uint16_t host_sleep_timeout_default = CONFIG_SLEEP_TIMEOUT_MS;
static uint32_t sleep_signal_transitions;
static enum sleep_hang_type timeout_hang_type;

static void sleep_transition_timeout(struct k_work *work);

static K_WORK_DELAYABLE_DEFINE(sleep_transition_timeout_data,
			       sleep_transition_timeout);

void power_chipset_handle_sleep_hang(enum sleep_hang_type hang_type)
{
#ifdef CONFIG_AP_SLP_S0_DEBUG
	LOG_ERR("Detected sleep hang cancel the slp_s0_debug_alarm"
		"and don't trigger EC_HOST_EVENT_HANG_DETECT\n");
	k_work_cancel_delayable(&slp_s0_debug_alarm_data);
	return;
#endif /* CONFIG_AP_SLP_S0_DEBUG */

	/*
	 * Wake up the AP so they don't just chill in a non-suspended state and
	 * burn power. Overload a vaguely related event bit since event bits are
	 * at a premium. If the system never entered S0ix, then manually set the
	 * wake mask to pretend it did, so that the hang detect event wakes the
	 * system.
	 */
#ifndef CONFIG_AP_PWRSEQ_DRIVER
	if (pwr_sm_get_state() == SYS_POWER_STATE_S0) {
		host_event_t sleep_wake_mask;

		ap_power_get_lazy_wake_mask(SYS_POWER_STATE_S0ix,
					    &sleep_wake_mask);
		lpc_set_host_event_mask(LPC_HOST_EVENT_WAKE, sleep_wake_mask);
	}
#else
	const struct device *dev = ap_pwrseq_get_instance();

	if (ap_pwrseq_get_current_state(dev) == AP_POWER_STATE_S0) {
		host_event_t sleep_wake_mask;

		ap_power_get_lazy_wake_mask(AP_POWER_STATE_S0IX,
					    &sleep_wake_mask);
		lpc_set_host_event_mask(LPC_HOST_EVENT_WAKE, sleep_wake_mask);
	}
#endif

	LOG_ERR("Detected sleep hang! Waking host up!");
	host_set_single_event(EC_HOST_EVENT_HANG_DETECT);
}

static void sleep_transition_timeout(struct k_work *work)
{
	/* Mark the timeout. */
	sleep_signal_transitions |= EC_HOST_RESUME_SLEEP_TIMEOUT;
	k_work_cancel_delayable(&sleep_transition_timeout_data);

	if (timeout_hang_type != SLEEP_HANG_NONE) {
		power_chipset_handle_sleep_hang(timeout_hang_type);
	}
}

static void sleep_increment_transition(void)
{
	if ((sleep_signal_transitions & EC_HOST_RESUME_SLEEP_TRANSITIONS_MASK) <
	    EC_HOST_RESUME_SLEEP_TRANSITIONS_MASK)
		sleep_signal_transitions += 1;
}

void sleep_suspend_transition(void)
{
	sleep_increment_transition();
	k_work_cancel_delayable(&sleep_transition_timeout_data);
}

void sleep_resume_transition(void)
{
	sleep_increment_transition();

	/*
	 * Start the timer again to ensure the AP doesn't get itself stuck in
	 * a state where it's no longer in a sleep state (S0ix/S3), but from
	 * the Linux perspective is still suspended. Perhaps a bug in the SoC-
	 * internal periodic housekeeping code might result in a situation
	 * like this.
	 */
	if (sleep_signal_timeout) {
		timeout_hang_type = SLEEP_HANG_S0IX_RESUME;
		k_work_schedule(&sleep_transition_timeout_data,
				K_MSEC(sleep_signal_timeout));
	}
}

void sleep_start_suspend(void)
{
	uint16_t timeout = host_get_sleep_timeout();

	sleep_signal_transitions = 0;

	/* Use 0xFFFF to disable the timeout */
	if (timeout == EC_HOST_SLEEP_TIMEOUT_INFINITE) {
		sleep_signal_timeout = 0;
		return;
	}

	/* Use zero internally to indicate host doesn't set timeout value;
	 * we will use default timeout.
	 */
	if (timeout == EC_HOST_SLEEP_TIMEOUT_DEFAULT) {
		timeout = host_sleep_timeout_default;
	}

	sleep_signal_timeout = timeout;
	timeout_hang_type = SLEEP_HANG_S0IX_SUSPEND;
	k_work_schedule(&sleep_transition_timeout_data, K_MSEC(timeout));
}

void sleep_complete_resume(void)
{
	/*
	 * Ensure we don't schedule another sleep_transition_timeout
	 * if the the HOST_SLEEP_EVENT_S0IX_RESUME message arrives before
	 * the CHIPSET task transitions to the POWER_S0ixS0 state.
	 */
	sleep_signal_timeout = 0;
	k_work_cancel_delayable(&sleep_transition_timeout_data);
	host_set_sleep_transitions(sleep_signal_transitions);
}

void sleep_reset_tracking(void)
{
	sleep_signal_transitions = 0;
	sleep_signal_timeout = 0;
	timeout_hang_type = SLEEP_HANG_NONE;
}

/*
 * s0ix event handler.
 */
static void ap_power_sleep_event_handler(struct ap_power_ev_callback *cb,
					 struct ap_power_ev_data data)
{
	switch (data.event) {
	case AP_POWER_S0IX_SUSPEND_START:
		sleep_start_suspend();
		break;
	case AP_POWER_S0IX_SUSPEND:
		sleep_suspend_transition();
		break;
	case AP_POWER_S0IX_RESUME:
		sleep_resume_transition();
		break;
	case AP_POWER_S0IX_RESUME_COMPLETE:
		sleep_complete_resume();
		break;
	case AP_POWER_S0IX_RESET_TRACKING:
		sleep_reset_tracking();
		break;
	default:
		break;
	}
}

/*
 * Registers callback for s0ix events.
 */
static int ap_power_sleep_s0ix_event(void)
{
	static struct ap_power_ev_callback cb;

	/*
	 * Register for all events.
	 */
	ap_power_ev_init_callback(
		&cb, ap_power_sleep_event_handler,
		AP_POWER_S0IX_SUSPEND_START | AP_POWER_S0IX_SUSPEND |
			AP_POWER_S0IX_RESUME | AP_POWER_S0IX_RESUME_COMPLETE |
			AP_POWER_S0IX_RESET_TRACKING);
	ap_power_ev_add_callback(&cb);
	return 0;
}
SYS_INIT(ap_power_sleep_s0ix_event, APPLICATION, 1);
