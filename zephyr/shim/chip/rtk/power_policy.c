/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "system.h"

#include <zephyr/kernel.h>
#include <zephyr/pm/pm.h>
#include <zephyr/pm/policy.h>

#include <soc.h>

/* CROS PM policy handler */
const struct pm_state_info *pm_policy_next_state(uint8_t cpu, int32_t ticks)
{
	ARG_UNUSED(cpu);

	const struct pm_state_info *ret = NULL;
	const struct pm_state_info *cpu_state_list;
	uint8_t num_cpu_states = pm_state_cpu_get_all(cpu, &cpu_state_list);

	if (DEEP_SLEEP_ALLOWED) {
		for (int i = 0; i < num_cpu_states; i++) {
			if (pm_policy_state_lock_is_active(
				    cpu_state_list[i].state,
				    PM_ALL_SUBSTATES)) {
				break;
			}
			ret = &cpu_state_list[i];
		}
	}
	return ret;
}
