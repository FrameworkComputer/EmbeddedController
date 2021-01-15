/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <kernel.h>
#include <init.h>
#include <sys/atomic.h>

#include "common.h"
#include "task.h"

/* We need to ensure that is one lower priority for the deferred task */
BUILD_ASSERT(CONFIG_NUM_PREEMPT_PRIORITIES + 1 >= TASK_ID_COUNT,
	     "Must increase number of available preempt priorities");

/* Declare all task stacks here */
#define CROS_EC_TASK(name, e, p, size) \
	K_THREAD_STACK_DEFINE(name##_STACK, size);
#define TASK_TEST(name, e, p, size) CROS_EC_TASK(name, e, p, size)
CROS_EC_TASK_LIST
#undef CROS_EC_TASK
#undef TASK_TEST

/* Forward declare all task entry point functions */
#define CROS_EC_TASK(name, entry, ...) void entry(void *p);
#define TASK_TEST(name, entry, ...) CROS_EC_TASK(name, entry)
CROS_EC_TASK_LIST
#undef CROS_EC_TASK
#undef TASK_TEST

/** Context for each CROS EC task that is run in its own zephyr thread */
struct task_ctx {
	/** Name of thread (for debugging) */
	const char *name;
	/** Zephyr thread structure that hosts EC tasks */
	struct k_thread zephyr_thread;
	/** Zephyr thread id for above thread */
	k_tid_t zephyr_tid;
	/** Address of Zephyr thread's stack */
	k_thread_stack_t *stack;
	/** Usabled size in bytes of above thread stack */
	size_t stack_size;
	/** Task (platform/ec) entry point */
	void (*entry)(void *p);
	/** The parameter that is passed into the task entry point */
	intptr_t parameter;
	/** A wait-able event that is raised when a new task event is posted */
	struct k_poll_signal new_event;
	/** The current platform/ec events set for this task/thread */
	uint32_t event_mask;
};

#define CROS_EC_TASK(_name, _entry, _parameter, _size) \
	{                                              \
		.entry = _entry,                       \
		.parameter = _parameter,               \
		.stack = _name##_STACK,                \
		.stack_size = _size,                   \
		.name = #_name,                        \
	},
#define TASK_TEST(_name, _entry, _parameter, _size) \
	CROS_EC_TASK(_name, _entry, _parameter, _size)
static struct task_ctx shimmed_tasks[] = { CROS_EC_TASK_LIST };
#undef CROS_EC_TASK
#undef TASK_TEST

task_id_t task_get_current(void)
{
	for (size_t i = 0; i < ARRAY_SIZE(shimmed_tasks); ++i) {
		if (shimmed_tasks[i].zephyr_tid == k_current_get()) {
			return i;
		}
	}
	__ASSERT(false, "Task index out of bound");
	return 0;
}

uint32_t *task_get_event_bitmap(task_id_t cros_task_id)
{
	struct task_ctx *const ctx = &shimmed_tasks[cros_task_id];

	return &ctx->event_mask;
}

uint32_t task_set_event(task_id_t cros_task_id, uint32_t event)
{
	struct task_ctx *const ctx = &shimmed_tasks[cros_task_id];

	atomic_or(&ctx->event_mask, event);
	k_poll_signal_raise(&ctx->new_event, 0);

	return 0;
}

uint32_t task_wait_event(int timeout_us)
{
	struct task_ctx *const ctx = &shimmed_tasks[task_get_current()];
	const k_timeout_t timeout = (timeout_us == -1) ? K_FOREVER :
							 K_USEC(timeout_us);
	const int64_t tick_deadline =
		k_uptime_ticks() + k_us_to_ticks_near64(timeout_us);

	struct k_poll_event poll_events[1] = {
		K_POLL_EVENT_INITIALIZER(K_POLL_TYPE_SIGNAL,
					 K_POLL_MODE_NOTIFY_ONLY,
					 &ctx->new_event),
	};

	/* Wait for signal, then clear it before reading events */
	const int rv = k_poll(poll_events, ARRAY_SIZE(poll_events), timeout);

	k_poll_signal_reset(&ctx->new_event);
	uint32_t events = atomic_set(&ctx->event_mask, 0);

	if (rv == -EAGAIN) {
		events |= TASK_EVENT_TIMER;
	}

	/* If we didn't get an event, we need to wait again. There is a very
	 * small change of us reading the event_mask one signaled event too
	 * early. In that case, just wait again for the remaining timeout
	 */
	if (events == 0) {
		const int64_t ticks_left = tick_deadline - k_uptime_ticks();

		if (ticks_left > 0) {
			return task_wait_event(
				k_ticks_to_us_near64(ticks_left));
		}

		events |= TASK_EVENT_TIMER;
	}

	return events;
}

uint32_t task_wait_event_mask(uint32_t event_mask, int timeout_us)
{
	struct task_ctx *const ctx = &shimmed_tasks[task_get_current()];
	uint32_t events = 0;
	const int64_t tick_deadline =
		k_uptime_ticks() + k_us_to_ticks_near64(timeout_us);

	/*  Need to return timeout flags if it occurs as well */
	event_mask |= TASK_EVENT_TIMER;

	while (!(event_mask & events)) {
		const int64_t ticks_left = tick_deadline - k_uptime_ticks();

		if (timeout_us != -1 && ticks_left <= 0) {
			events |= TASK_EVENT_TIMER;
			break;
		}

		struct k_poll_event poll_events[1] = {
			K_POLL_EVENT_INITIALIZER(K_POLL_TYPE_SIGNAL,
						 K_POLL_MODE_NOTIFY_ONLY,
						 &ctx->new_event),
		};

		/* Ensure to honor the -1 timeout as FOREVER */
		k_poll(poll_events, ARRAY_SIZE(poll_events),
		       timeout_us == -1 ? K_FOREVER : K_TICKS(ticks_left));
		k_poll_signal_reset(&ctx->new_event);
		events |= atomic_set(&ctx->event_mask, 0);
	}

	/* Replace any events that weren't in the mask */
	if (events & ~event_mask) {
		atomic_or(&ctx->event_mask, events & ~event_mask);
		k_poll_signal_raise(&ctx->new_event, 0);
	}

	return events & event_mask;
}

static void task_entry(void *task_contex, void *unused1, void *unused2)
{
	ARG_UNUSED(unused1);
	ARG_UNUSED(unused2);

	struct task_ctx *const ctx = (struct task_ctx *)task_contex;
	/* Name thread for debugging */
	k_thread_name_set(ctx->zephyr_tid, ctx->name);

	/* Call into task entry point */
	ctx->entry((void *)ctx->parameter);
}

void start_ec_tasks(void)
{
	for (size_t i = 0; i < ARRAY_SIZE(shimmed_tasks); ++i) {
		struct task_ctx *const ctx = &shimmed_tasks[i];

		/*
		 * TODO(b/172361873): Add K_FP_REGS for FPU tasks. See
		 * comment in config.h for CONFIG_TASK_LIST for existing flags
		 * implementation.
		 */
		ctx->zephyr_tid = k_thread_create(
			&ctx->zephyr_thread, ctx->stack, ctx->stack_size,
			task_entry, ctx, NULL, NULL,
			K_PRIO_PREEMPT(TASK_ID_COUNT - i - 1), 0, K_NO_WAIT);
	}
}

/*
 * Initialize all of the kernel objects before application code starts.
 * This allows us to set events on tasks before they even start, e.g. in
 * INIT_HOOKS.
 */
int init_signals(const struct device *unused)
{
	ARG_UNUSED(unused);

	for (size_t i = 0; i < ARRAY_SIZE(shimmed_tasks); ++i) {
		struct task_ctx *const ctx = &shimmed_tasks[i];

		/* Initialize the new_event structure */
		k_poll_signal_init(&ctx->new_event);
	}

	return 0;
}
SYS_INIT(init_signals, POST_KERNEL, 50);

int task_start_called(void)
{
	return 1;
}

void interrupt_disable(void)
{
	/*
	 * TODO (b:174481378) system.c needed an implementation of this. Though
	 * it's not yet clear where we call interrupt_enable() from. These two
	 * calls should be replaced with irq_lock and irq_unlock.
	 */
}
