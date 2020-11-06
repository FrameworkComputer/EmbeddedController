/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <kernel.h>
#include <ztest.h>

#include "ec_tasks.h"
#include "task.h"

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

static void task_get_current1(void)
{
	zassert_equal(task_get_current(), TASK_ID_TASK_1, "ID matches");
}

static void task_get_current2(void)
{
	zassert_equal(task_get_current(), TASK_ID_TASK_2, "ID matches");
}

static void test_task_get_current(void)
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

static void test_timeout(void)
{
	run_test(&timeout1, &timeout2);
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

	task_set_event(TASK_ID_TASK_1, 0x1234, 0);
}

static void test_event_delivered(void)
{
	run_test(&event_delivered1, &event_delivered2);
}


static void event_mask_not_delivered1(void)
{
	task_set_event(TASK_ID_TASK_2, 0x007F, 0);
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

static void test_event_mask_not_delivered(void)
{
	run_test(&event_mask_not_delivered1, &event_mask_not_delivered2);
}


static void event_mask_extra1(void)
{
	k_sleep(K_SECONDS(1));

	task_set_event(TASK_ID_TASK_2, 0x00FF, 0);
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

static void test_event_mask_extra(void)
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
	task_set_event(TASK_ID_TASK_2, 0, 0);
	k_sleep(K_SECONDS(1));
	task_set_event(TASK_ID_TASK_2, 0x1234, 0);
}

static void empty_set_mask2(void)
{
	const uint32_t start_ms = k_uptime_get();
	const uint32_t events = task_wait_event_mask(0x1234, TASK_SEC(10));
	const uint32_t end_ms = k_uptime_get();

	zassert_equal(events, 0x1234, "Verify only waited for event");
	zassert_within(end_ms - start_ms, 2000, 100, "Timeout for 2 seconds");
}

static void test_empty_set_mask(void)
{
	run_test(&empty_set_mask1, &empty_set_mask2);
}


void test_main(void)
{
	/* Manually start the EC tasks. This normally happens in main. */
	start_ec_tasks();

	ztest_test_suite(test_task_shim,
			 ztest_unit_test(test_task_get_current),
			 ztest_unit_test(test_timeout),
			 ztest_unit_test(test_event_delivered),
			 ztest_unit_test(test_event_mask_not_delivered),
			 ztest_unit_test(test_event_mask_extra),
			 ztest_unit_test(test_empty_set_mask));
	ztest_run_test_suite(test_task_shim);
}
