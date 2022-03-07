/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <kernel.h>
#include <zephyr.h>

#include <ap_power/ap_power.h>
#include <ap_power/ap_power_events.h>
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

static void check_hook_task_priority(void)
{
	k_tid_t thread = &k_sys_work_q.thread;

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
DECLARE_HOOK(HOOK_INIT, check_hook_task_priority, HOOK_PRIO_FIRST);

static int zephyr_shim_setup_hooks(const struct device *unused)
{
	int rv;

	STRUCT_SECTION_FOREACH(zephyr_shim_hook_list, entry) {
		struct zephyr_shim_hook_list **loc =
			&hook_registry[entry->info->type];

		/* Find the correct place to put the entry in the registry. */
		while (*loc && (*loc)->info->priority < entry->info->priority)
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

	return 0;
}

SYS_INIT(zephyr_shim_setup_hooks, APPLICATION, 1);

void hook_notify(enum hook_type type)
{
	struct zephyr_shim_hook_list *p;

	for (p = hook_registry[type]; p; p = p->next)
		p->info->routine();
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

/*
 * Shims for interconnecting AP power sequence events with legacy hooks.
 * Depending on whether the power sequence code is running in zephyr or
 * not, a shim is setup to send events either from the legacy hooks to
 * the AP power event callbacks, or vice versa.
 */
#ifdef CONFIG_AP_PWRSEQ

/*
 * Callback handler, dispatch to hooks
 */
static void ev_handler(struct ap_power_ev_callback *cb,
		       struct ap_power_ev_data data)
{
	switch (data.event) {
	default:
		break;

#define CASE_HOOK(h)				\
	case AP_POWER_##h:				\
		hook_notify(HOOK_CHIPSET_##h);	\
		break

	CASE_HOOK(PRE_INIT);
	CASE_HOOK(STARTUP);
	CASE_HOOK(RESUME);
	CASE_HOOK(SUSPEND);
#ifdef CONFIG_CHIPSET_RESUME_INIT_HOOK
	CASE_HOOK(RESUME_INIT);
	CASE_HOOK(SUSPEND_COMPLETE);
#endif
	CASE_HOOK(SHUTDOWN);
	CASE_HOOK(SHUTDOWN_COMPLETE);
	CASE_HOOK(HARD_OFF);
	CASE_HOOK(RESET);
	}
}

/*
 * Events are received from the AP power event system and sent to the hooks.
 */
static int zephyr_shim_ap_power_event(const struct device *unused)
{
	static struct ap_power_ev_callback cb;

	/*
	 * Register for all events.
	 */
	ap_power_ev_init_callback(&cb, ev_handler,
				  AP_POWER_PRE_INIT |
				  AP_POWER_STARTUP |
				  AP_POWER_RESUME |
				  AP_POWER_SUSPEND |
#ifdef CONFIG_CHIPSET_RESUME_INIT_HOOK
				  AP_POWER_RESUME_INIT |
				  AP_POWER_SUSPEND_COMPLETE |
#endif
				  AP_POWER_SHUTDOWN |
				  AP_POWER_SHUTDOWN_COMPLETE |
				  AP_POWER_HARD_OFF |
				  AP_POWER_RESET);
	ap_power_ev_add_callback(&cb);
	return 0;
}

SYS_INIT(zephyr_shim_ap_power_event, APPLICATION, 1);
#else /* !CONFIG_AP_PWRSEQ */

/*
 * Events received from the hooks and sent to the AP power event callbacks.
 */
#define EV_HOOK(h)						\
static void hook_##h(void)					\
{								\
	ap_power_ev_send_callbacks(AP_POWER_##h);			\
}								\
DECLARE_HOOK(HOOK_CHIPSET_##h, hook_##h, HOOK_PRIO_DEFAULT)

EV_HOOK(PRE_INIT);
EV_HOOK(STARTUP);
EV_HOOK(RESUME);
EV_HOOK(SUSPEND);
#ifdef CONFIG_CHIPSET_RESUME_INIT_HOOK
EV_HOOK(RESUME_INIT);
EV_HOOK(SUSPEND_COMPLETE);
#endif
EV_HOOK(SHUTDOWN);
EV_HOOK(SHUTDOWN_COMPLETE);
EV_HOOK(HARD_OFF);
EV_HOOK(RESET);
#endif /* !CONFIG_AP_PWRSEQ */
