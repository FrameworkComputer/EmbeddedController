/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/kernel.h>
#include <zephyr/init.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/shell/shell.h>

#include "common.h"
#include "host_command.h"
#include "timer.h"
#include "task.h"

/* Ensure that the idle task is at lower priority than lowest priority task. */
BUILD_ASSERT(EC_TASK_PRIORITY(EC_TASK_PRIO_LOWEST) < K_IDLE_PRIO,
	     "CONFIG_NUM_PREEMPT_PRIORITIES too small, some tasks would run at "
	     "idle priority");

/* Forward declare all task entry point functions */
#define CROS_EC_TASK(name, entry, ...) void entry(void *p);
#define TASK_TEST(name, entry, ...) CROS_EC_TASK(name, entry)
CROS_EC_TASK_LIST
#undef CROS_EC_TASK
#undef TASK_TEST

/* Statically declare all threads here */
#define CROS_EC_TASK(name, entry, parameter, stack_size, priority)      \
	K_THREAD_DEFINE(name, stack_size, entry, parameter, NULL, NULL, \
			EC_TASK_PRIORITY(priority), 0, -1);
#define TASK_TEST(name, e, p, size) CROS_EC_TASK(name, e, p, size)
CROS_EC_TASK_LIST
#undef CROS_EC_TASK
#undef TASK_TEST

struct task_ctx_base_data {
	/** A wait-able event that is raised when a new task event is posted */
	struct k_poll_signal new_event;
	/** The current platform/ec events set for this task/thread */
	atomic_t event_mask;
};

/*
 * Create a mapping from the cros-ec task ID to the Zephyr thread.
 */
#undef CROS_EC_TASK
#define CROS_EC_TASK(_name, _entry, _parameter, _size, _prio) _name,
#ifdef TEST_BUILD
static k_tid_t task_to_k_tid[TASK_ID_COUNT] = {
#else
const static k_tid_t task_to_k_tid[TASK_ID_COUNT] = {
#endif
	CROS_EC_TASK_LIST
};

static struct task_ctx_base_data shimmed_tasks_data[TASK_ID_COUNT];
static struct task_ctx_base_data extra_tasks_data[EXTRA_TASK_COUNT];
/* Task timer structures. Keep separate from the context ones to avoid memory
 * holes due to int64_t fields in struct _timeout.
 */
static struct k_timer shimmed_tasks_timers[TASK_ID_COUNT + EXTRA_TASK_COUNT];

static int tasks_started;
#undef CROS_EC_TASK
#undef TASK_TEST

static struct task_ctx_base_data *task_get_base_data(task_id_t cros_task_id)
{
	if (cros_task_id >= TASK_ID_COUNT + EXTRA_TASK_COUNT) {
		return NULL;
	}

	if (cros_task_id >= TASK_ID_COUNT) {
		return &extra_tasks_data[cros_task_id - TASK_ID_COUNT];
	}

	return &shimmed_tasks_data[cros_task_id];
}

task_id_t task_get_current(void)
{
	if (in_deferred_context()) {
		return TASK_ID_SYSWORKQ;
	}

#ifdef CONFIG_TASK_HOSTCMD_THREAD_MAIN
	if (in_host_command_main()) {
		return TASK_ID_HOSTCMD;
	}
#endif

	for (size_t i = 0; i < TASK_ID_COUNT; ++i) {
		if (task_to_k_tid[i] == k_current_get())
			return i;
	}

	__ASSERT(false, "Task index out of bound");
	return 0;
}

atomic_t *task_get_event_bitmap(task_id_t cros_task_id)
{
	struct task_ctx_base_data *data;

	data = task_get_base_data(cros_task_id);

	return &data->event_mask;
}

void task_set_event(task_id_t cros_task_id, uint32_t event)
{
	struct task_ctx_base_data *data;

	data = task_get_base_data(cros_task_id);

	atomic_or(&data->event_mask, event);
	k_poll_signal_raise(&data->new_event, 0);
}

uint32_t task_wait_event(int timeout_us)
{
	struct task_ctx_base_data *data;

	data = task_get_base_data(task_get_current());

	const k_timeout_t timeout = (timeout_us == -1) ? K_FOREVER :
							 K_USEC(timeout_us);
	const int64_t tick_deadline =
		k_uptime_ticks() + k_us_to_ticks_near64(timeout_us);

	struct k_poll_event poll_events[1] = {
		K_POLL_EVENT_INITIALIZER(K_POLL_TYPE_SIGNAL,
					 K_POLL_MODE_NOTIFY_ONLY,
					 &data->new_event),
	};

	/* Wait for signal, then clear it before reading events */
	const int rv = k_poll(poll_events, ARRAY_SIZE(poll_events), timeout);

	k_poll_signal_reset(&data->new_event);
	uint32_t events = atomic_set(&data->event_mask, 0);

	if (rv == -EAGAIN) {
		events |= TASK_EVENT_TIMER;
	}

	/* If we didn't get an event, we need to wait again. There is a very
	 * small chance of us reading the event_mask one signaled event too
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
	struct task_ctx_base_data *data;

	data = task_get_base_data(task_get_current());

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
						 &data->new_event),
		};

		/* Ensure to honor the -1 timeout as FOREVER */
		k_poll(poll_events, ARRAY_SIZE(poll_events),
		       timeout_us == -1 ? K_FOREVER : K_TICKS(ticks_left));
		k_poll_signal_reset(&data->new_event);
		events |= atomic_set(&data->event_mask, 0);
	}

	/* Replace any events that weren't in the mask */
	if (events & ~event_mask) {
		atomic_or(&data->event_mask, events & ~event_mask);
		k_poll_signal_raise(&data->new_event, 0);
	}

	return events & event_mask;
}

/*
 * Callback function to use with k_timer_start to set the
 * TASK_EVENT_TIMER event on a task.
 */
static void timer_expire(struct k_timer *timer_id)
{
	task_id_t cros_ec_task_id = timer_id - shimmed_tasks_timers;

	task_set_event(cros_ec_task_id, TASK_EVENT_TIMER);
}

int timer_arm(timestamp_t event, task_id_t cros_ec_task_id)
{
	struct k_timer *timer;
	timestamp_t now = get_time();

	timer = &shimmed_tasks_timers[cros_ec_task_id];

	if (event.val <= now.val) {
		/* Timer requested for now or in the past, fire right away */
		task_set_event(cros_ec_task_id, TASK_EVENT_TIMER);
		return EC_SUCCESS;
	}

	/* Check for a running timer */
	if (k_timer_remaining_get(timer))
		return EC_ERROR_BUSY;

	k_timer_start(timer, K_USEC(event.val - now.val), K_NO_WAIT);
	return EC_SUCCESS;
}

void timer_cancel(task_id_t cros_ec_task_id)
{
	struct k_timer *timer;

	timer = &shimmed_tasks_timers[cros_ec_task_id];

	k_timer_stop(timer);
}

#ifdef TEST_BUILD
void set_test_runner_tid(void)
{
	task_to_k_tid[TASK_ID_TEST_RUNNER] = k_current_get();
}

#ifdef CONFIG_TASKS_SET_TEST_RUNNER_TID_RULE
#include <zephyr/ztest.h>
static void set_test_runner_tid_rule_before(const struct ztest_unit_test *test,
					    void *data)
{
	ARG_UNUSED(test);
	ARG_UNUSED(data);
	set_test_runner_tid();
}

ZTEST_RULE(set_test_runner_tid, set_test_runner_tid_rule_before, NULL);
#endif /* CONFIG_TASKS_SET_TEST_RUNNER_TID_RULE */
#endif /* TEST_BUILD */

void start_ec_tasks(void)
{
	for (size_t i = 0; i < TASK_ID_COUNT + EXTRA_TASK_COUNT; ++i) {
		k_timer_init(&shimmed_tasks_timers[i], timer_expire, NULL);
	}

	for (size_t i = 0; i < TASK_ID_COUNT; ++i) {
#ifdef TEST_BUILD
		/* The test runner thread is automatically started. */
		if (i == TASK_ID_TEST_RUNNER) {
			continue;
		}
#endif
		k_thread_start(task_to_k_tid[i]);
	}

	tasks_started = 1;
}

/*
 * Initialize all of the kernel objects before application code starts.
 * This allows us to set events on tasks before they even start, e.g. in
 * INIT_HOOKS.
 */
int init_signals(const struct device *unused)
{
	ARG_UNUSED(unused);

	for (size_t i = 0; i < TASK_ID_COUNT + EXTRA_TASK_COUNT; ++i) {
		struct task_ctx_base_data *const data = task_get_base_data(i);

		k_poll_signal_init(&data->new_event);
	}

	return 0;
}
SYS_INIT(init_signals, POST_KERNEL, 50);

int task_start_called(void)
{
	return tasks_started;
}

void task_disable_task(task_id_t tskid)
{
	/* TODO(b/190203712): Implement this */
}

void task_clear_pending_irq(int irq)
{
#if CONFIG_ITE_IT8XXX2_INTC
	ite_intc_isr_clear(irq);
#endif
}

void task_enable_irq(int irq)
{
	arch_irq_enable(irq);
}

inline bool in_interrupt_context(void)
{
	return k_is_in_isr();
}

inline bool in_deferred_context(void)
{
	/*
	 * Deferred calls run in the sysworkq.
	 */
	return (k_current_get() == &k_sys_work_q.thread);
}

#if IS_ENABLED(CONFIG_KERNEL_SHELL) && IS_ENABLED(CONFIG_THREAD_MONITOR)
static int taskinfo(const struct shell *shell, size_t argc, char **argv)
{
	return shell_execute_cmd(shell, "kernel threads");
}
SHELL_CMD_REGISTER(taskinfo, NULL, "Threads statistics", taskinfo);
#endif
