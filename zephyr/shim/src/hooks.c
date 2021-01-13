/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <kernel.h>
#include <zephyr.h>

#include "common.h"
#include "console.h"
#include "hooks.h"
#include "task.h"
#include "timer.h"

#define DEFERRED_STACK_SIZE 1024

/*
 * Deferred thread is always the lowest priority, and preemptive if
 * available.
 */
#ifdef CONFIG_PREEMPT_ENABLED
#define DEFERRED_THREAD_PRIORITY (CONFIG_NUM_PREEMPT_PRIORITIES - 1)
#else
#define DEFERRED_THREAD_PRIORITY -1
#endif

static K_THREAD_STACK_DEFINE(deferred_thread, DEFERRED_STACK_SIZE);
static struct k_work_q deferred_work_queue;

static void deferred_work_queue_handler(struct k_work *work)
{
	struct deferred_data *data =
		CONTAINER_OF(work, struct deferred_data, delayed_work.work);

	data->routine();
}

static int init_deferred_work_queue(const struct device *unused)
{
	ARG_UNUSED(unused);
	k_work_q_start(&deferred_work_queue, deferred_thread,
		       DEFERRED_STACK_SIZE, DEFERRED_THREAD_PRIORITY);
	return 0;
}
SYS_INIT(init_deferred_work_queue, APPLICATION, 0);

void zephyr_shim_setup_deferred(const struct deferred_data *data)
{
	struct deferred_data *non_const = (struct deferred_data *)data;

	k_delayed_work_init(&non_const->delayed_work,
			    deferred_work_queue_handler);
}

int hook_call_deferred(const struct deferred_data *data, int us)
{
	struct deferred_data *non_const = (struct deferred_data *)data;
	int rv;

	rv = k_delayed_work_submit_to_queue(&deferred_work_queue,
					    &non_const->delayed_work,
					    K_USEC(us));
	if (rv < 0)
		cprints(CC_HOOK, "Warning: deferred call not submitted.");
	return rv;
}

static struct zephyr_shim_hook_list *hook_registry[HOOK_TYPE_COUNT];

void zephyr_shim_setup_hook(enum hook_type type, void (*routine)(void),
			    int priority, struct zephyr_shim_hook_list *entry)
{
	struct zephyr_shim_hook_list **loc = &hook_registry[type];

	/* Find the correct place to put the entry in the registry. */
	while (*loc && (*loc)->priority < priority)
		loc = &((*loc)->next);

	/* Setup the entry. */
	entry->routine = routine;
	entry->priority = priority;
	entry->next = *loc;

	/* Insert the entry. */
	*loc = entry;
}

void hook_notify(enum hook_type type)
{
	struct zephyr_shim_hook_list *p;

	for (p = hook_registry[type]; p; p = p->next)
		p->routine();
}

void hook_task(void *u)
{
	/* Periodic hooks will be called first time through the loop */
	static uint64_t last_second = -SECOND;
	static uint64_t last_tick = -HOOK_TICK_INTERVAL;

	while (1) {
		uint64_t t = get_time().val;
		int next = 0;

		if (t - last_tick >= HOOK_TICK_INTERVAL) {
			hook_notify(HOOK_TICK);
			last_tick = t;
		}

		if (t - last_second >= SECOND) {
			hook_notify(HOOK_SECOND);
			last_second = t;
		}

		/* Calculate when next tick needs to occur */
		t = get_time().val;
		if (last_tick + HOOK_TICK_INTERVAL > t)
			next = last_tick + HOOK_TICK_INTERVAL - t;

		/*
		 * Sleep until next tick, unless we've already exceeded
		 * HOOK_TICK_INTERVAL.
		 */
		if (next > 0)
			task_wait_event(next);
	}
}

