/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <ap_power/ap_power_interface.h>
#include <ap_power/ap_pwrseq.h>
#include <x86_non_dsx_common_pwrseq_sm_handler.h>

LOG_MODULE_DECLARE(ap_pwrseq, CONFIG_AP_PWRSEQ_LOG_LEVEL);

#if CONFIG_PLATFORM_EC_HOST_INTERFACE_ESPI

/* If host doesn't program S0ix lazy wake mask, use default S0ix mask */
#define DEFAULT_WAKE_MASK_S0IX                        \
	(EC_HOST_EVENT_MASK(EC_HOST_EVENT_LID_OPEN) | \
	 EC_HOST_EVENT_MASK(EC_HOST_EVENT_MODE_CHANGE))

/*
 * Set the wake mask according to the current power state:
 * 1. On transition to S0, wake mask is reset.
 * 2. In non-S0 states, active mask set by host gets a higher preference.
 * 3. If host has not set any active mask, then check if a lazy mask exists
 *    for the current power state.
 * 4. If state is S0ix and no lazy or active wake mask is set, then use default
 *    S0ix mask to be compatible with older BIOS versions.
 */
#ifndef CONFIG_AP_PWRSEQ_DRIVER
void power_update_wake_mask(void)
{
	host_event_t wake_mask;
	enum power_states_ndsx state;

	state = pwr_sm_get_state();

	if (state == SYS_POWER_STATE_S0)
		wake_mask = 0;
	else if (lpc_is_active_wm_set_by_host() ||
		 ap_power_get_lazy_wake_mask(state, &wake_mask))
		return;
#if CONFIG_AP_PWRSEQ_S0IX
	if ((state == SYS_POWER_STATE_S0ix) && (wake_mask == 0))
		wake_mask = DEFAULT_WAKE_MASK_S0IX;
#endif

	lpc_set_host_event_mask(LPC_HOST_EVENT_WAKE, wake_mask);
}
#else
void power_update_wake_mask(void)
{
	const struct device *dev = ap_pwrseq_get_instance();
	host_event_t wake_mask;
	enum ap_pwrseq_state state;

	state = ap_pwrseq_get_current_state(dev);

	if (state == AP_POWER_STATE_S0)
		wake_mask = 0;
	else if (lpc_is_active_wm_set_by_host() ||
		 ap_power_get_lazy_wake_mask(state, &wake_mask))
		return;
#if CONFIG_AP_PWRSEQ_S0IX
	if ((state == AP_POWER_STATE_S0IX) && (wake_mask == 0))
		wake_mask = DEFAULT_WAKE_MASK_S0IX;
#endif

	lpc_set_host_event_mask(LPC_HOST_EVENT_WAKE, wake_mask);
}
#endif /* CONFIG_AP_PWRSEQ_DRIVER */

static void power_update_wake_mask_deferred(struct k_work *work)
{
	power_update_wake_mask();
}

static K_WORK_DELAYABLE_DEFINE(power_update_wake_mask_deferred_data,
			       power_update_wake_mask_deferred);

void ap_power_set_active_wake_mask(void)
{
	int rv;

	/*
	 * Allow state machine to stabilize and update wake mask after 5msec. It
	 * was observed that on platforms where host wakes up periodically from
	 * S0ix for hardware book-keeping activities, there is a small window
	 * where host is not really up and running software, but still SLP_S0#
	 * is de-asserted and hence setting wake mask right away can cause user
	 * wake events to be missed.
	 *
	 * Time for deferred callback was chosen to be 5msec based on the fact
	 * that it takes ~2msec for the periodic wake cycle to complete on the
	 * host for KBL.
	 */
	rv = k_work_schedule(&power_update_wake_mask_deferred_data, K_MSEC(5));
	if (rv == 0) {
		/*
		 * A work is already scheduled or submitted, since power state
		 * has changed again and the work is not processed, we should
		 * reschedule it.
		 */
		rv = k_work_reschedule(&power_update_wake_mask_deferred_data,
				       K_MSEC(5));
	}
	__ASSERT(rv >= 0, "Set wake mask work queue error");
}

#else /* CONFIG_PLATFORM_EC_HOST_INTERFACE_ESPI */
static void ap_power_set_active_wake_mask(void)
{
}
#endif /* CONFIG_PLATFORM_EC_HOST_INTERFACE_ESPI */

#if CONFIG_AP_PWRSEQ_S0IX
/*
 * Backup copies of SCI and SMI mask to preserve across S0ix suspend/resume
 * cycle. If the host uses S0ix, BIOS is not involved during suspend and resume
 * operations and hence SCI/SMI masks are programmed only once during boot-up.
 *
 * These backup variables are set whenever host expresses its interest to
 * enter S0ix and then lpc_host_event_mask for SCI and SMI are cleared. When
 * host resumes from S0ix, masks from backup variables are copied over to
 * lpc_host_event_mask for SCI and SMI.
 */
static host_event_t backup_sci_mask;
static host_event_t backup_smi_mask;

/* Flag to notify listeners about suspend/resume events. */
enum ap_power_sleep_type sleep_state = AP_POWER_SLEEP_NONE;

/*
 * Clear host event masks for SMI and SCI when host is entering S0ix. This is
 * done to prevent any SCI/SMI interrupts when the host is in suspend. Since
 * BIOS is not involved in the suspend path, EC needs to take care of clearing
 * these masks.
 */
static void power_s0ix_suspend_clear_masks(void)
{
	host_event_t sci_mask, smi_mask;

	sci_mask = lpc_get_host_event_mask(LPC_HOST_EVENT_SCI);
	smi_mask = lpc_get_host_event_mask(LPC_HOST_EVENT_SMI);

	/* Do not backup already-cleared SCI/SMI masks. */
	if (!sci_mask && !smi_mask)
		return;

	backup_sci_mask = sci_mask;
	backup_smi_mask = smi_mask;
	lpc_set_host_event_mask(LPC_HOST_EVENT_SCI, 0);
	lpc_set_host_event_mask(LPC_HOST_EVENT_SMI, 0);
}

/*
 * Restore host event masks for SMI and SCI when host exits S0ix. This is done
 * because BIOS is not involved in the resume path and so EC needs to restore
 * the masks from backup variables.
 */
static void power_s0ix_resume_restore_masks(void)
{
	/*
	 * No need to restore SCI/SMI masks if both backup_sci_mask and
	 * backup_smi_mask are zero. This indicates that there was a failure to
	 * enter S0ix(SLP_S0# assertion) and hence SCI/SMI masks were never
	 * backed up.
	 */
	if (!backup_sci_mask && !backup_smi_mask)
		return;
	lpc_set_host_event_mask(LPC_HOST_EVENT_SCI, backup_sci_mask);
	lpc_set_host_event_mask(LPC_HOST_EVENT_SMI, backup_smi_mask);
	backup_sci_mask = backup_smi_mask = 0;
}

/*
 * Following functions are called in the S0ix path, not S3 path.
 */

/*
 * Notify the sleep type that is going to transit to; this is a token to
 * ensure both host sleep event passed by Host Command and SLP_S0 satisfy
 * the conditions to suspend or resume.
 *
 * @param new_state Notified sleep type
 */
static void ap_power_sleep_set_notify(enum ap_power_sleep_type new_state)
{
	sleep_state = new_state;
}

enum ap_power_sleep_type ap_power_sleep_get_notify(void)
{
	return sleep_state;
}

#ifdef CONFIG_AP_PWRSEQ_S0IX_COUNTER
atomic_t s0ix_counter;
#endif

void ap_power_sleep_notify_transition(enum ap_power_sleep_type check_state)
{
	if (sleep_state != check_state)
		return;

	if (check_state == AP_POWER_SLEEP_SUSPEND) {
		/*
		 * Transition to S0ix;
		 * clear mask before others running suspend.
		 */
		power_s0ix_suspend_clear_masks();
		ap_power_ev_send_callbacks(AP_POWER_SUSPEND);
#ifdef CONFIG_AP_PWRSEQ_S0IX_COUNTER
		atomic_inc(&s0ix_counter);
#endif
	} else if (check_state == AP_POWER_SLEEP_RESUME) {
		ap_power_ev_send_callbacks(AP_POWER_RESUME);
		/*
		 * Transition is done; reset sleep state after resume.
		 */
		ap_power_sleep_set_notify(AP_POWER_SLEEP_NONE);
	}
}
#endif /* CONFIG_AP_PWRSEQ_S0IX */

#if CONFIG_AP_PWRSEQ_HOST_SLEEP
#define HOST_SLEEP_EVENT_DEFAULT_RESET 0

static struct host_sleep_event_context *g_ctx;

void ap_power_reset_host_sleep_state(void)
{
	power_set_host_sleep_state(HOST_SLEEP_EVENT_DEFAULT_RESET);
	ap_power_ev_send_callbacks(AP_POWER_S0IX_RESET_TRACKING);
	ap_power_chipset_handle_host_sleep_event(HOST_SLEEP_EVENT_DEFAULT_RESET,
						 NULL);
}

/* TODO: hook to reset event */
void ap_power_handle_chipset_reset(void)
{
	if (ap_power_in_state(AP_POWER_STATE_STANDBY))
		ap_power_reset_host_sleep_state();
}

void ap_power_chipset_handle_host_sleep_event(
	enum host_sleep_event state, struct host_sleep_event_context *ctx)
{
	LOG_INF("host sleep event = %d", state);

	g_ctx = ctx;

#if CONFIG_AP_PWRSEQ_S0IX
	if (state == HOST_SLEEP_EVENT_S0IX_SUSPEND) {
		/*
		 * Indicate to power state machine that a new host event for
		 * s0ix/s3 suspend has been received and so chipset suspend
		 * notification needs to be sent to listeners.
		 */
		ap_power_sleep_set_notify(AP_POWER_SLEEP_SUSPEND);
		ap_power_ev_send_callbacks(AP_POWER_S0IX_SUSPEND_START);
		power_signal_enable(PWR_SLP_S0);
	} else if (state == HOST_SLEEP_EVENT_S0IX_RESUME) {
		/*
		 * Set sleep state to resume; restore SCI/SMI masks;
		 * SLP_S0 should be de-asserted already, disable interrupt.
		 */
		ap_power_sleep_set_notify(AP_POWER_SLEEP_RESUME);
		power_s0ix_resume_restore_masks();
		power_signal_disable(PWR_SLP_S0);
		ap_power_ev_send_callbacks(AP_POWER_S0IX_RESUME_COMPLETE);

		/*
		 * If the sleep signal timed out and never transitioned, then
		 * the wake mask was modified to its suspend state (S0ix), so
		 * that the event wakes the system. Explicitly restore the wake
		 * mask to its S0 state now.
		 */
		power_update_wake_mask();

	} else if (state == HOST_SLEEP_EVENT_DEFAULT_RESET) {
		power_signal_disable(PWR_SLP_S0);
	}
#endif /* CONFIG_AP_PWRSEQ_S0IX */

#ifndef CONFIG_AP_PWRSEQ_DRIVER
	ap_pwrseq_wake();
#else
	ap_pwrseq_post_event(ap_pwrseq_get_instance(), AP_PWRSEQ_EVENT_HOST);
#endif /* CONFIG_AP_PWRSEQ_DRIVER */
}

uint16_t host_get_sleep_timeout(void)
{
	return g_ctx->sleep_timeout_ms;
}

void host_set_sleep_transitions(uint32_t val)
{
	g_ctx->sleep_transitions = val;
}

#endif /* CONFIG_AP_PWRSEQ_HOST_SLEEP */
