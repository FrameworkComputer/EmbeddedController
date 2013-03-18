/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* System hooks for Chrome EC */

#include "atomic.h"
#include "console.h"
#include "hooks.h"
#include "link_defs.h"
#include "timer.h"
#include "util.h"

struct hook_ptrs {
	const struct hook_data *start;
	const struct hook_data *end;
};

/*
 * Hook data start and end pointers for each type of hook.  Must be in same
 * order as enum hook_type.
 */
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
	{__hooks_tick, __hooks_tick_end},
	{__hooks_second, __hooks_second_end},
};

static uint32_t pending_hooks;

/**
 * Actual notification function
 */
static void notify(enum hook_type type)
{
	const struct hook_data *start, *end, *p;
	int count, called = 0;
	int last_prio = HOOK_PRIO_FIRST - 1, prio;

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
				p->routine();
			}
		}
	}
}

void hook_notify(enum hook_type type)
{
	if (type == HOOK_AC_CHANGE) {
		/* Store deferred hook and wake task */
		atomic_or(&pending_hooks, 1 << type);
		task_wake(TASK_ID_TICK);
	} else {
		/* Notify now */
		notify(type);
	}
}

void hook_task(void)
{
	/* Periodic hooks will be called first time through the loop */
	static uint64_t last_second = -SECOND;
	static uint64_t last_tick = -HOOK_TICK_INTERVAL;

	while (1) {
		uint64_t t = get_time().val;
		uint32_t pending = atomic_read_clear(&pending_hooks);
		int i;

		/* Call pending hooks, if any */
		for (i = 0; pending && i < 32; i++) {
			const uint32_t mask = 1 << i;

			if (pending & mask) {
				notify(i);
				pending ^= mask;
			}
		}

		if (t - last_tick >= HOOK_TICK_INTERVAL) {
			notify(HOOK_TICK);
			last_tick = t;
		}

		if (t - last_second >= SECOND) {
			notify(HOOK_SECOND);
			last_second = t;
		}

		/* Use up the rest of our hook tick interval */
		t = get_time().val - t;
		if (t < HOOK_TICK_INTERVAL)
			usleep(HOOK_TICK_INTERVAL - t);
	}
}
