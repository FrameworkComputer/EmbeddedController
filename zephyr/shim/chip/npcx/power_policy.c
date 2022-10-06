/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/kernel.h>
#include <zephyr/pm/pm.h>
#include <zephyr/pm/policy.h>
#include <soc.h>

#include "console.h"
#include "cros_version.h"
#include "system.h"

static const struct pm_state_info residency_info[] =
	PM_STATE_INFO_LIST_FROM_DT_CPU(DT_NODELABEL(cpu0));

/* CROS PM policy handler */
const struct pm_state_info *pm_policy_next_state(uint8_t cpu, int32_t ticks)
{
	ARG_UNUSED(cpu);

	if (DEEP_SLEEP_ALLOWED) {
		for (int i = ARRAY_SIZE(residency_info) - 1; i >= 0; i--) {
			if (pm_policy_state_lock_is_active(
				    residency_info[i].state,
				    PM_ALL_SUBSTATES)) {
				continue;
			}

			/* Find suitable power state by residency time */
			if (ticks == K_TICKS_FOREVER ||
			    ticks >= k_us_to_ticks_ceil32(
					     residency_info[i]
						     .min_residency_us)) {
				return &residency_info[i];
			}
		}
	}

	return NULL;
}
