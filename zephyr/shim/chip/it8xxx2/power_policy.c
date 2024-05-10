/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * TODO(b/272518464): Work around coreboot GCC preprocessor bug.
 * #line marks the *next* line, so it is off by one.
 */
#line 11

#include "system.h"

#include <zephyr/kernel.h>
#include <zephyr/pm/pm.h>
#include <zephyr/pm/policy.h>

#include <soc.h>

static const struct pm_state_info pm_states[] =
	PM_STATE_INFO_LIST_FROM_DT_CPU(DT_NODELABEL(cpu0));

/* CROS PM policy handler */
const struct pm_state_info *pm_policy_next_state(uint8_t cpu, int32_t ticks)
{
	ARG_UNUSED(cpu);

	/* Deep sleep is allowed */
	if (DEEP_SLEEP_ALLOWED) {
		/*
		 * If there are multiple power states, iterating backward
		 * is needed to take priority into account.
		 */
		for (int i = 0; i < ARRAY_SIZE(pm_states); i++) {
			/*
			 * To check if given power state is enabled and
			 * could be used.
			 */
			if (pm_policy_state_lock_is_active(pm_states[i].state,
							   PM_ALL_SUBSTATES)) {
				continue;
			}

			return &pm_states[i];
		}
	}

	return NULL;
}
