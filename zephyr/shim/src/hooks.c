/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <kernel.h>
#include <zephyr.h>

#include "common.h"
#include "console.h"
#include "ec_tasks.h"
#include "hooks.h"
#include "task.h"
#include "timer.h"

static void hook_second_work(struct k_work *work);
static void hook_tick_work(struct k_work *work);

static struct zephyr_shim_hook_list *hook_registry[HOOK_TYPE_COUNT];

static K_WORK_DELAYABLE_DEFINE(hook_seconds_work_data, hook_second_work);
static K_WORK_DELAYABLE_DEFINE(hook_ticks_work_data, hook_tick_work);

static void work_queue_error(const void *data, int rv)
{
	cprints(CC_HOOK,
		"Warning: deferred call not submitted, "
		"deferred_data=0x%pP, err=%d",
		data, rv);
}

static void hook_second_work(struct k_work *work)
{
	int rv;

	hook_notify(HOOK_SECOND);

	rv = k_work_reschedule(&hook_seconds_work_data, K_SECONDS(1));
	if (rv < 0)
		work_queue_error(&hook_seconds_work_data, rv);
}

static void hook_tick_work(struct k_work *work)
{
	int rv;

	hook_notify(HOOK_TICK);

	rv = k_work_reschedule(&hook_ticks_work_data,
			       K_USEC(HOOK_TICK_INTERVAL));
	if (rv < 0)
		work_queue_error(&hook_ticks_work_data, rv);
}

static void check_hook_task_priority(k_tid_t thread)
{
	/*
	 * Numerically lower priorities take precedence, so verify the hook
	 * related threads cannot preempt any of the shimmed tasks.
	 */
	if (k_thread_priority_get(thread) < (TASK_ID_COUNT - 1))
		cprintf(CC_HOOK,
			"ERROR: %s has priority %d but must be >= %d\n",
			k_thread_name_get(thread),
			k_thread_priority_get(thread), (TASK_ID_COUNT - 1));
}

static int zephyr_shim_setup_hooks(const struct device *unused)
{
	int rv;

	STRUCT_SECTION_FOREACH(zephyr_shim_hook_list, entry) {
		struct zephyr_shim_hook_list **loc =
			&hook_registry[entry->type];

		/* Find the correct place to put the entry in the registry. */
		while (*loc && (*loc)->priority < entry->priority)
			loc = &((*loc)->next);

		entry->next = *loc;

		/* Insert the entry. */
		*loc = entry;
	}

	/* Startup the HOOK_TICK and HOOK_SECOND recurring work */
	rv = k_work_reschedule(&hook_seconds_work_data, K_SECONDS(1));
	if (rv < 0)
		work_queue_error(&hook_seconds_work_data, rv);

	rv = k_work_reschedule(&hook_ticks_work_data,
			       K_USEC(HOOK_TICK_INTERVAL));
	if (rv < 0)
		work_queue_error(&hook_ticks_work_data, rv);

	check_hook_task_priority(&k_sys_work_q.thread);

	return 0;
}

SYS_INIT(zephyr_shim_setup_hooks, APPLICATION, 1);

void hook_notify(enum hook_type type)
{
	struct zephyr_shim_hook_list *p;

	for (p = hook_registry[type]; p; p = p->next)
		p->routine();
}

int hook_call_deferred(const struct deferred_data *data, int us)
{
	struct k_work_delayable *work = data->work;
	int rv = 0;

	if (us == -1) {
		k_work_cancel_delayable(work);
	} else if (us >= 0) {
		rv = k_work_reschedule(work, K_USEC(us));
		if (rv == -EINVAL) {
			/* Already processing or completed. */
			return 0;
		} else if (rv < 0) {
			work_queue_error(data, rv);
		}
	} else {
		return EC_ERROR_PARAM2;
	}

	return rv;
}
