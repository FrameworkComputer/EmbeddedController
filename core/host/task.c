/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Task scheduling / events module for Chrome EC operating system */

#include <execinfo.h>
#include <malloc.h>
#include <pthread.h>
#include <semaphore.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "atomic.h"
#include "common.h"
#include "console.h"
#include "host_test.h"
#include "task.h"
#include "task_id.h"
#include "test_util.h"
#include "timer.h"

#define SIGNAL_INTERRUPT SIGUSR1

#define SIGNAL_TRACE_DUMP SIGTERM
#define MAX_TRACE 30
/*
 * When trace dump is requested from signal handler, skip:
 *   _task_dump_trace_impl
 *   _task_dump_trace_dispath
 *   A function in libc
 */
#define SIGNAL_TRACE_OFFSET 3
/*
 * When trace dump is requested from task_dump_trace(), skip:
 *   task_dump_trace
 *   _task_dump_trace_impl
 */
#define DIRECT_TRACE_OFFSET 2

struct emu_task_t {
	pthread_t thread;
	pthread_cond_t resume;
	uint32_t event;
	timestamp_t wake_time;
	uint8_t started;
};

struct task_args {
	void (*routine)(void *);
	void *d;
};

static struct emu_task_t tasks[TASK_ID_COUNT];
static pthread_cond_t scheduler_cond;
static pthread_mutex_t run_lock;
static task_id_t running_task_id;
static int task_started;
static pthread_t main_thread;

static sem_t interrupt_sem;
static pthread_mutex_t interrupt_lock;
static pthread_t interrupt_thread;
static int in_interrupt;
static int interrupt_disabled;
static void (*pending_isr)(void);
static int generator_sleeping;
static timestamp_t generator_sleep_deadline;
static int has_interrupt_generator = 1;

static __thread task_id_t my_task_id; /* thread local task id */

#define TASK(n, r, d, s) void r(void *);
CONFIG_TASK_LIST
CONFIG_TEST_TASK_LIST
#undef TASK

/* Idle task */
void __idle(void *d)
{
	while (1)
		task_wait_event(-1);
}

void _run_test(void *d)
{
	run_test();
}

#define TASK(n, r, d, s) {r, d},
struct task_args task_info[TASK_ID_COUNT] = {
	{__idle, NULL},
	CONFIG_TASK_LIST
	CONFIG_TEST_TASK_LIST
	{_run_test, NULL},
};
#undef TASK

#define TASK(n, r, d, s) #n,
static const char * const task_names[] = {
	"<< idle >>",
	CONFIG_TASK_LIST
	CONFIG_TEST_TASK_LIST
	"<< test runner >>",
};
#undef TASK

void task_pre_init(void)
{
	/* Nothing */
}

int in_interrupt_context(void)
{
	return !!in_interrupt;
}

void interrupt_disable(void)
{
	pthread_mutex_lock(&interrupt_lock);
	interrupt_disabled = 1;
	pthread_mutex_unlock(&interrupt_lock);
}

void interrupt_enable(void)
{
	pthread_mutex_lock(&interrupt_lock);
	interrupt_disabled = 0;
	pthread_mutex_unlock(&interrupt_lock);
}

static void _task_execute_isr(int sig)
{
	in_interrupt = 1;
	pending_isr();
	sem_post(&interrupt_sem);
	in_interrupt = 0;
}

static void __attribute__((noinline)) _task_dump_trace_impl(int offset)
{
	void *trace[MAX_TRACE];
	size_t sz;
	char **messages;
	char buf[256];
	FILE *file;
	int i, nb;

	sz = backtrace(trace, MAX_TRACE);
	messages = backtrace_symbols(trace + offset, sz - offset);

	for (i = 0; i < sz - offset; ++i) {
		fprintf(stderr, "#%-2d %s\n", i, messages[i]);
		sprintf(buf, "addr2line %p -e %s",
			trace[i + offset], __get_prog_name());
		file = popen(buf, "r");
		if (file) {
			nb = fread(buf, 1, sizeof(buf) - 1, file);
			buf[nb] = '\0';
			fprintf(stderr, "    %s", buf);
			pclose(file);
		}
	}
	fflush(stderr);
	free(messages);
}

void __attribute__((noinline)) task_dump_trace(void)
{
	_task_dump_trace_impl(DIRECT_TRACE_OFFSET);
}

static void __attribute__((noinline)) _task_dump_trace_dispatch(int sig)
{
	int need_dispatch = 1;

	if (!pthread_equal(pthread_self(), main_thread)) {
		need_dispatch = 0;
	} else if (!task_start_called()) {
		fprintf(stderr, "Stack trace of main thread:\n");
		need_dispatch = 0;
	} else if (in_interrupt_context()) {
		fprintf(stderr, "Stack trace of ISR:\n");
	} else {
		fprintf(stderr, "Stack trace of task %d (%s):\n",
				running_task_id, task_names[running_task_id]);
	}

	if (need_dispatch) {
		pthread_kill(tasks[running_task_id].thread, SIGNAL_TRACE_DUMP);
	} else {
		_task_dump_trace_impl(SIGNAL_TRACE_OFFSET);
		udelay(100 * MSEC); /* Leave time for stderr to flush */
		exit(1);
	}
}

void task_register_tracedump(void)
{
	/* Trace dumper MUST be registered from main thread */
	main_thread = pthread_self();
	signal(SIGNAL_TRACE_DUMP, _task_dump_trace_dispatch);
}

static void task_register_interrupt(void)
{
	sem_init(&interrupt_sem, 0, 0);
	signal(SIGNAL_INTERRUPT, _task_execute_isr);
}

void task_trigger_test_interrupt(void (*isr)(void))
{
	pthread_mutex_lock(&interrupt_lock);
	if (interrupt_disabled) {
		pthread_mutex_unlock(&interrupt_lock);
		return;
	}

	/* Suspend current task and excute ISR */
	pending_isr = isr;
	pthread_kill(tasks[running_task_id].thread, SIGNAL_INTERRUPT);

	/* Wait for ISR to complete */
	sem_wait(&interrupt_sem);
	while (in_interrupt)
		;
	pending_isr = NULL;

	pthread_mutex_unlock(&interrupt_lock);
}

void interrupt_generator_udelay(unsigned us)
{
	generator_sleep_deadline.val = get_time().val + us;
	generator_sleeping = 1;
	while (get_time().val < generator_sleep_deadline.val)
		;
	generator_sleeping = 0;
}

uint32_t task_set_event(task_id_t tskid, uint32_t event, int wait)
{
	tasks[tskid].event = event;
	if (wait)
		return task_wait_event(-1);
	return 0;
}

uint32_t task_wait_event(int timeout_us)
{
	int tid = task_get_current();
	int ret;
	pthread_mutex_lock(&interrupt_lock);
	if (timeout_us > 0)
		tasks[tid].wake_time.val = get_time().val + timeout_us;

	/* Transfer control to scheduler */
	pthread_cond_signal(&scheduler_cond);
	pthread_cond_wait(&tasks[tid].resume, &run_lock);

	/* Resume */
	ret = tasks[tid].event;
	tasks[tid].event = 0;
	pthread_mutex_unlock(&interrupt_lock);
	return ret;
}

void mutex_lock(struct mutex *mtx)
{
	int value = 0;
	int id = 1 << task_get_current();

	mtx->waiters |= id;

	do {
		if (mtx->lock == 0) {
			mtx->lock = 1;
			value = 1;
		}

		if (!value)
			task_wait_event(-1);
	} while (!value);

	mtx->waiters &= ~id;
}

void mutex_unlock(struct mutex *mtx)
{
	int v;
	mtx->lock = 0;

	for (v = 31; v >= 0; --v)
		if ((1ul << v) & mtx->waiters) {
			mtx->waiters &= ~(1ul << v);
			task_set_event(v, TASK_EVENT_MUTEX, 0);
			break;
		}
}

task_id_t task_get_current(void)
{
	return my_task_id;
}

void wait_for_task_started(void)
{
	int i, ok;

	while (1) {
		ok = 1;
		for (i = 0; i < TASK_ID_COUNT - 1; ++i)
			if (!tasks[i].started) {
				msleep(10);
				ok = 0;
				break;
			}
		if (ok)
			return;
	}
}

static task_id_t task_get_next_wake(void)
{
	int i;
	timestamp_t min_time;
	int which_task = TASK_ID_INVALID;

	min_time.val = ~0ull;

	for (i = TASK_ID_COUNT - 1; i >= 0; --i)
		if (min_time.val >= tasks[i].wake_time.val) {
			min_time.val = tasks[i].wake_time.val;
			which_task = i;
		}

	return which_task;
}

static int fast_forward(void)
{
	/*
	 * No task has event pending, and thus the next time we have an
	 * event to process must be either of:
	 *   1. Interrupt generator triggers an interrupt
	 *   2. The next wake alarm is reached
	 * So we should check whether an interrupt may happen, and fast
	 * forward to the nearest among:
	 *   1. When interrupt generator wakes up
	 *   2. When the next task wakes up
	 */
	int task_id = task_get_next_wake();

	if (!has_interrupt_generator) {
		if (task_id == TASK_ID_INVALID) {
			return TASK_ID_IDLE;
		} else {
			force_time(tasks[task_id].wake_time);
			return task_id;
		}
	}

	if (!generator_sleeping)
		return TASK_ID_IDLE;

	if (task_id != TASK_ID_INVALID &&
	    tasks[task_id].wake_time.val < generator_sleep_deadline.val) {
		force_time(tasks[task_id].wake_time);
		return task_id;
	} else {
		force_time(generator_sleep_deadline);
		return TASK_ID_IDLE;
	}
}

int task_start_called(void)
{
	return task_started;
}

void task_scheduler(void)
{
	int i;
	timestamp_t now;

	task_started = 1;

	while (1) {
		now = get_time();
		i = TASK_ID_COUNT - 1;
		while (i >= 0) {
			if (tasks[i].event || now.val >= tasks[i].wake_time.val)
				break;
			--i;
		}
		if (i < 0)
			i = fast_forward();

		tasks[i].wake_time.val = ~0ull;
		running_task_id = i;
		tasks[i].started = 1;
		pthread_cond_signal(&tasks[i].resume);
		pthread_cond_wait(&scheduler_cond, &run_lock);
	}
}

void *_task_start_impl(void *a)
{
	long tid = (long)a;
	struct task_args *arg = task_info + tid;
	my_task_id = tid;
	pthread_mutex_lock(&run_lock);

	/* Wait for scheduler */
	task_wait_event(1);
	tasks[tid].event = 0;

	/* Start the task routine */
	(arg->routine)(arg->d);

	/* Catch exited routine */
	while (1)
		task_wait_event(-1);
}

test_mockable void interrupt_generator(void)
{
	has_interrupt_generator = 0;
}

void *_task_int_generator_start(void *d)
{
	my_task_id = TASK_ID_INT_GEN;
	interrupt_generator();
	return NULL;
}

int task_start(void)
{
	int i;

	task_register_interrupt();

	pthread_mutex_init(&run_lock, NULL);
	pthread_mutex_init(&interrupt_lock, NULL);
	pthread_cond_init(&scheduler_cond, NULL);

	pthread_mutex_lock(&run_lock);

	for (i = 0; i < TASK_ID_COUNT; ++i) {
		tasks[i].event = TASK_EVENT_WAKE;
		tasks[i].wake_time.val = ~0ull;
		tasks[i].started = 0;
		pthread_cond_init(&tasks[i].resume, NULL);
		pthread_create(&tasks[i].thread, NULL, _task_start_impl,
			       (void *)(uintptr_t)i);
		pthread_cond_wait(&scheduler_cond, &run_lock);
		/*
		 * Interrupt lock is grabbed by the task which just started.
		 * Let's unlock it so the next task can be started.
		 */
		pthread_mutex_unlock(&interrupt_lock);
	}

	/*
	 * All tasks are now waiting in task_wait_event(). Lock interrupt_lock
	 * here so the first task chosen sees it locked.
	 */
	pthread_mutex_lock(&interrupt_lock);

	pthread_create(&interrupt_thread, NULL,
		       _task_int_generator_start, NULL);

	task_scheduler();

	return 0;
}
