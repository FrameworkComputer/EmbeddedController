/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "ec_tasks.h"
#include "task.h"
#include "timer.h"

#include <stdbool.h>

#include <zephyr/kernel.h>
#include <zephyr/kernel/thread.h>
#include <zephyr/ztest.h>

/* Second for platform/ec task API (in microseconds). */
#define TASK_SEC(s) (s * 1000 * 1000)

K_SEM_DEFINE(task_done1, 0, 1);
K_SEM_DEFINE(task_done2, 0, 1);
K_SEM_DEFINE(test_ready1, 0, 1);
K_SEM_DEFINE(test_ready2, 0, 1);

static void (*task1)(void);
static void (*task2)(void);

static void run_test(void (*task1_run)(void), void (*task2_run)(void))
{
	task1 = task1_run;
	task2 = task2_run;
	k_sem_give(&test_ready1);
	k_sem_give(&test_ready2);
	k_sem_take(&task_done1, K_FOREVER);
	k_sem_take(&task_done2, K_FOREVER);
}

void task1_entry(void *p)
{
	while (1) {
		k_sem_take(&test_ready1, K_FOREVER);
		task1();
		k_sem_give(&task_done1);
	}
}

void task2_entry(void *p)
{
	while (1) {
		k_sem_take(&test_ready2, K_FOREVER);
		task2();
		k_sem_give(&task_done2);
	}
}

/*
 * Unlike Tasks 1 & 2, it is allowed to run Task 3 more than once per
 * call to run_test().  It will call task3_entry_func if set, and wait
 * for the next event.  This is useful to test things like timers,
 * which you are expecting the event to fire at some point in the
 * future, and you want to test that it happens.
 */
static void (*task3_entry_func)(uint32_t event_mask);

void task3_entry(void *p)
{
	uint32_t events = 0;

	for (;;) {
		if (task3_entry_func)
			task3_entry_func(events);
		events = task_wait_event(-1);
	}
}

static void set_event_before_task_start1(void)
{
	const uint32_t events = task_wait_event(TASK_SEC(2));

	zassert_equal(events, 0xAAAA, "Should have 0xAAAA events");
}

static void set_event_before_task_start2(void)
{
	/* Do nothing */
}

static void *tasks_setup(void)
{
	start_ec_tasks();

	return NULL;
}

ZTEST(test_task_shim, test_set_event_before_task_start)
{
	/* Send event before tasks start */
	task_set_event(TASK_ID_TASK_1, 0xAAAA);

	run_test(set_event_before_task_start1, set_event_before_task_start2);
}

static void task_get_current1(void)
{
	zassert_equal(task_get_current(), TASK_ID_TASK_1, "ID matches");
}

static void task_get_current2(void)
{
	zassert_equal(task_get_current(), TASK_ID_TASK_2, "ID matches");
}

ZTEST(test_task_shim, test_task_get_current)
{
	run_test(&task_get_current1, &task_get_current2);
}

static void timeout1(void)
{
	const uint32_t start_ms = k_uptime_get();
	const uint32_t events = task_wait_event(TASK_SEC(2));
	const uint32_t end_ms = k_uptime_get();

	zassert_equal(events, TASK_EVENT_TIMER, "Should have timeout event");
	zassert_within(end_ms - start_ms, 2000, 100, "Timeout for 2 seconds");
}

static void timeout2(void)
{
	/* Do nothing */
}

ZTEST(test_task_shim, test_timeout)
{
	run_test(&timeout1, &timeout2);
}

/*
 * Timer test:
 *   1. Task 1 arms a timer for Task 3 in expiring 2 seconds.
 *   2. Task 2 does nothing.
 *   3. Task 3 validates that the it receives a TASK_EVENT_TIMER event
 *      2 seconds after Task 1 armed the timer (within 100ms
 *      tolerance).
 */
static timestamp_t timer_armed_at;
K_SEM_DEFINE(check_timer_finished, 0, 1);

static void check_timer(uint32_t event_mask)
{
	timestamp_t now = get_time();

	zassert_equal(event_mask & TASK_EVENT_TIMER, TASK_EVENT_TIMER,
		      "Timer event mask should be set");
	zassert_within(now.val - timer_armed_at.val, TASK_SEC(2),
		       TASK_SEC(1) / 10,
		       "Timer should expire at 2 seconds from arm time");
	k_sem_give(&check_timer_finished);
}

static void timer_task_1(void)
{
	timestamp_t timer_timeout;

	timer_armed_at = get_time();

	timer_timeout.val = timer_armed_at.val + TASK_SEC(2);

	task3_entry_func = check_timer;
	zassert_equal(timer_arm(timer_timeout, TASK_ID_TASK_3), EC_SUCCESS,
		      "Setting timer should succeed");
}

static void timer_task_2(void)
{
	/* Do nothing */
}

ZTEST(test_task_shim, test_timer)
{
	run_test(timer_task_1, timer_task_2);
	zassert_equal(k_sem_take(&check_timer_finished, K_SECONDS(4 * 1000)), 0,
		      "Task 3 did not finish within timeout");
	zassert_equal(task3_entry_func, check_timer,
		      "check_timer should have been enabled");
	task3_entry_func = NULL;
}

static void event_delivered1(void)
{
	const uint32_t start_ms = k_uptime_get();
	const uint32_t events = task_wait_event(-1);
	const uint32_t end_ms = k_uptime_get();

	zassert_equal(events, 0x1234, "Verify event bits");
	zassert_within(end_ms - start_ms, 5000, 100, "Waited for 5 seconds");
}

static void event_delivered2(void)
{
	k_sleep(K_SECONDS(5));

	task_set_event(TASK_ID_TASK_1, 0x1234);
}

ZTEST(test_task_shim, test_event_delivered)
{
	run_test(&event_delivered1, &event_delivered2);
}

static void event_mask_not_delivered1(void)
{
	task_set_event(TASK_ID_TASK_2, 0x007F);
}

static void event_mask_not_delivered2(void)
{
	const uint32_t start_ms = k_uptime_get();
	const uint32_t events = task_wait_event_mask(0x0080, TASK_SEC(7));
	const uint32_t end_ms = k_uptime_get();

	zassert_equal(events, TASK_EVENT_TIMER, "Should have timeout event");
	zassert_within(end_ms - start_ms, 7000, 100, "Timeout for 7 seconds");

	const uint32_t leftover_events = task_wait_event(0);

	zassert_equal(leftover_events, 0x007F, "All events should be waiting");
}

ZTEST(test_task_shim, test_event_mask_not_delivered)
{
	run_test(&event_mask_not_delivered1, &event_mask_not_delivered2);
}

static void event_mask_extra1(void)
{
	k_sleep(K_SECONDS(1));

	task_set_event(TASK_ID_TASK_2, 0x00FF);
}

static void event_mask_extra2(void)
{
	const uint32_t start_ms = k_uptime_get();
	const uint32_t events = task_wait_event_mask(0x0001, TASK_SEC(10));
	const uint32_t end_ms = k_uptime_get();

	zassert_equal(events, 0x0001, "Verify only waited for event");
	zassert_within(end_ms - start_ms, 1000, 100, "Timeout for 1 second");

	const uint32_t leftover_events = task_wait_event(0);

	zassert_equal(leftover_events, 0x00FE, "All events should be waiting");
}

ZTEST(test_task_shim, test_event_mask_extra)
{
	run_test(&event_mask_extra1, &event_mask_extra2);
}

static void empty_set_mask1(void)
{
	k_sleep(K_SECONDS(1));
	/*
	 * It is generally invalid to set a 0 event, but this simulates a race
	 * condition and exercises fallback code in task_wait_event
	 */
	task_set_event(TASK_ID_TASK_2, 0);
	k_sleep(K_SECONDS(1));
	task_set_event(TASK_ID_TASK_2, 0x1234);
}

static void empty_set_mask2(void)
{
	const uint32_t start_ms = k_uptime_get();
	const uint32_t events = task_wait_event_mask(0x1234, TASK_SEC(10));
	const uint32_t end_ms = k_uptime_get();

	zassert_equal(events, 0x1234, "Verify only waited for event");
	zassert_within(end_ms - start_ms, 2000, 100, "Timeout for 2 seconds");
}

static void check_task_1_mapping(void)
{
	zassert_equal(TASK_ID_TASK_1, thread_id_to_task_id(k_current_get()));
	zassert_equal(k_current_get(), task_id_to_thread_id(TASK_ID_TASK_1));
}

static void check_task_2_mapping(void)
{
	zassert_equal(TASK_ID_TASK_2, thread_id_to_task_id(k_current_get()));
	zassert_equal(k_current_get(), task_id_to_thread_id(TASK_ID_TASK_2));
}

ZTEST(test_task_shim, test_thread_to_task_mapping)
{
	run_test(&check_task_1_mapping, &check_task_2_mapping);
}

ZTEST(test_task_shim, test_empty_set_mask)
{
	run_test(&empty_set_mask1, &empty_set_mask2);
}

ZTEST_SUITE(test_task_shim, NULL, tasks_setup, NULL, NULL, NULL);
