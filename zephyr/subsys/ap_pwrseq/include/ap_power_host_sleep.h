/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __AP_POWER_HOST_SLEEP_H
#define __AP_POWER_HOST_SLEEP_H

#include <ap_power/ap_power_interface.h>
#include <power_host_sleep.h>

/*
 * Deferred call to set active mask according to current power state
 */
void ap_power_set_active_wake_mask(void);

/*
 * Get lazy wake masks for the sleep state provided
 *
 * @param state Power state
 * @param mask  Lazy wake mask.
 *
 * @return 0 for success; -EINVAL if power state is not S3/S5/S0ix
 */
int ap_power_get_lazy_wake_mask(
	enum power_states_ndsx state, host_event_t *mask);

#if CONFIG_AP_PWRSEQ_S0IX
/* For S0ix path, flag to notify sleep change */
enum ap_power_sleep_type {
	AP_POWER_SLEEP_NONE,
	AP_POWER_SLEEP_SUSPEND,
	AP_POWER_SLEEP_RESUME,
};

/*
 * Reset host sleep state and clean up
 */
void ap_power_reset_host_sleep_state(void);

/*
 * Check if the sleep type current power transition indicates is the same
 * as what is notified. If same, means host sleep event notified by AP
 * through Host Command and SLP_S0 are consistent. Process
 * the transition. Otherwise, no action.
 *
 * @param check_state Sleep type which is going to transit to.
 */
void ap_power_sleep_notify_transition(enum ap_power_sleep_type check_state);
#endif /* CONFIG_AP_PWRSEQ_S0IX */

#endif /* __AP_PWRSEQ_HOST_SLEEP_H */
