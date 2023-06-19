/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include <zephyr/kernel.h>

#include <ap_power/ap_power_interface.h>
#include <power_host_sleep.h>

#ifndef CONFIG_AP_PWRSEQ_DRIVER
static enum power_state
translate_ap_power_state(enum power_states_ndsx ap_power_state)
{
	switch (ap_power_state) {
	case SYS_POWER_STATE_S5:
		return POWER_S5;
	case SYS_POWER_STATE_S3:
		return POWER_S3;
#if CONFIG_AP_PWRSEQ_S0IX
	case SYS_POWER_STATE_S0ix:
		return POWER_S0ix;
#endif
	default:
		return 0;
	}
}

int ap_power_get_lazy_wake_mask(enum power_states_ndsx state,
				host_event_t *mask)
{
	enum power_state st;

	st = translate_ap_power_state(state);
	if (!st)
		return -EINVAL;
	return get_lazy_wake_mask(st, mask);
}
#else
#include "ap_power/ap_pwrseq.h"

static enum power_state
translate_ap_power_state(enum ap_pwrseq_state ap_power_state)
{
	switch (ap_power_state) {
	case AP_POWER_STATE_S5:
		return POWER_S5;
	case AP_POWER_STATE_S3:
		return POWER_S3;
#if CONFIG_AP_PWRSEQ_S0IX
	case AP_POWER_STATE_S0IX:
		return POWER_S0ix;
#endif
	default:
		return 0;
	}
}

int ap_power_get_lazy_wake_mask(enum ap_pwrseq_state state, host_event_t *mask)
{
	enum power_state st;

	st = translate_ap_power_state(state);
	if (!st)
		return -EINVAL;
	return get_lazy_wake_mask(st, mask);
}
#endif /* CONFIG_AP_PWRSEQ_DRIVER */

#if CONFIG_AP_PWRSEQ_HOST_SLEEP
void power_chipset_handle_host_sleep_event(enum host_sleep_event state,
					   struct host_sleep_event_context *ctx)
{
	ap_power_chipset_handle_host_sleep_event(state, ctx);
}
#endif
