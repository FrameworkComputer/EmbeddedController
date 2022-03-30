/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <ap_power/ap_power_interface.h>
#include <ap_power_host_sleep.h>
#include <x86_non_dsx_common_pwrseq_sm_handler.h>

LOG_MODULE_DECLARE(ap_pwrseq, CONFIG_AP_PWRSEQ_LOG_LEVEL);

#if CONFIG_PLATFORM_EC_HOST_INTERFACE_ESPI

/* If host doesn't program S0ix lazy wake mask, use default S0ix mask */
#define DEFAULT_WAKE_MASK_S0IX  (EC_HOST_EVENT_MASK(EC_HOST_EVENT_LID_OPEN) | \
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

static void power_update_wake_mask_deferred(struct k_work *work)
{
	power_update_wake_mask();
}

static K_WORK_DELAYABLE_DEFINE(
	power_update_wake_mask_deferred_data, power_update_wake_mask_deferred);

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
		rv = k_work_reschedule(
			&power_update_wake_mask_deferred_data, K_MSEC(5));
	}
	__ASSERT(rv >= 0, "Set wake mask work queue error");
}

#else /* CONFIG_PLATFORM_EC_HOST_INTERFACE_ESPI */
static void ap_power_set_active_wake_mask(void) { }
#endif /* CONFIG_PLATFORM_EC_HOST_INTERFACE_ESPI */

#if CONFIG_AP_PWRSEQ_HOST_SLEEP
#define HOST_SLEEP_EVENT_DEFAULT_RESET 0

void ap_power_reset_host_sleep_state(void)
{
	power_set_host_sleep_state(HOST_SLEEP_EVENT_DEFAULT_RESET);
	ap_power_chipset_handle_host_sleep_event(
			HOST_SLEEP_EVENT_DEFAULT_RESET, NULL);
}

/* TODO: hook to reset event */
void ap_power_handle_chipset_reset(void)
{
	if (ap_power_in_state(AP_POWER_STATE_STANDBY))
		ap_power_reset_host_sleep_state();
}

void ap_power_chipset_handle_host_sleep_event(
		enum host_sleep_event state,
		struct host_sleep_event_context *ctx)
{
	LOG_DBG("host sleep event = %d!", state);
}
#endif /* CONFIG_AP_PWRSEQ_HOST_SLEEP */
