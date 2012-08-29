/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* System hooks for Chrome EC */

#include "hooks.h"
#include "link_defs.h"
#include "util.h"

struct hook_ptrs {
	const struct hook_data *start;
	const struct hook_data *end;
};

/* Hook data start and end pointers for each type of hook.  Must be in same
 * order as enum hook_type. */
static const struct hook_ptrs hook_list[] = {
	{__hooks_init, __hooks_init_end},
	{__hooks_freq_change, __hooks_freq_change_end},
	{__hooks_sysjump, __hooks_sysjump_end},
	{__hooks_chipset_pre_init, __hooks_chipset_pre_init_end},
	{__hooks_chipset_startup, __hooks_chipset_startup_end},
	{__hooks_chipset_resume, __hooks_chipset_resume_end},
	{__hooks_chipset_suspend, __hooks_chipset_suspend_end},
	{__hooks_chipset_shutdown, __hooks_chipset_shutdown_end},
	{__hooks_ac_change, __hooks_ac_change_end},
	{__hooks_lid_change, __hooks_lid_change_end},
};


int hook_notify(enum hook_type type, int stop_on_error)
{
	const struct hook_data *start, *end, *p;
	int count, called = 0;
	int last_prio = HOOK_PRIO_FIRST - 1, prio;
	int rv_error = EC_SUCCESS, rv;

	start = hook_list[type].start;
	end = hook_list[type].end;
	count = ((uint32_t)end - (uint32_t)start) / sizeof(struct hook_data);

	/* Call all the hooks in priority order */
	while (called < count) {
		/* Find the lowest remaining priority */
		for (p = start, prio = HOOK_PRIO_LAST + 1; p < end; p++) {
			if (p->priority < prio && p->priority > last_prio)
				prio = p->priority;
		}
		last_prio = prio;

		/* Call all the hooks with that priority */
		for (p = start; p < end; p++) {
			if (p->priority == prio) {
				called++;
				rv = p->routine();
				if (rv != EC_SUCCESS) {
					if (stop_on_error)
						return rv;
					else if (rv_error == EC_SUCCESS)
						rv_error = rv;
				}
			}
		}
	}

	/* Return the first error seen, if any */
	return rv_error;
}
