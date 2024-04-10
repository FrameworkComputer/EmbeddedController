/* Copyright 2013 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Test hooks.
 */

#include "common.h"
#include "console.h"
#include "hooks.h"
#include "test_util.h"
#include "timer.h"
#include "util.h"

static int init_hook_count;
static int init_early_hook_count;
static bool init_early_before_init;
static int tick_hook_count;
static int tick2_hook_count;
static int tick_count_seen_by_tick2;
static timestamp_t tick_time[2];
static int second_hook_count;
static timestamp_t second_time[2];
static int deferred_call_count;

static void init_hook(void)
{
	init_hook_count++;
}
DECLARE_HOOK(HOOK_INIT, init_hook, HOOK_PRIO_DEFAULT);

static void init_early_hook(void)
{
	init_early_hook_count++;
	init_early_before_init = init_hook_count == 0;
}
DECLARE_HOOK(HOOK_INIT_EARLY, init_early_hook, HOOK_PRIO_DEFAULT);

static void tick_hook(void)
{
	tick_hook_count++;
	tick_time[0] = tick_time[1];
	tick_time[1] = get_time();
}
DECLARE_HOOK(HOOK_TICK, tick_hook, HOOK_PRIO_DEFAULT);

static void tick2_hook(void)
{
	tick2_hook_count++;
	tick_count_seen_by_tick2 = tick_hook_count;
}
/* tick2_hook() prio means it should be called after tick_hook() */
DECLARE_HOOK(HOOK_TICK, tick2_hook, HOOK_PRIO_DEFAULT + 1);

static void second_hook(void)
{
	second_hook_count++;
	second_time[0] = second_time[1];
	second_time[1] = get_time();
}
DECLARE_HOOK(HOOK_SECOND, second_hook, HOOK_PRIO_DEFAULT);

static void deferred_func(void)
{
	deferred_call_count++;
}
DECLARE_DEFERRED(deferred_func);

static void non_deferred_func(void)
{
	deferred_call_count++;
}

static const struct deferred_data non_deferred_func_data = { non_deferred_func };

static int test_init_hook(void)
{
	TEST_ASSERT(init_hook_count == 1);
	return EC_SUCCESS;
}

static int test_init_early_hook(void)
{
	TEST_ASSERT(init_early_hook_count == 1);
	TEST_ASSERT(init_early_before_init == true);
	return EC_SUCCESS;
}

static int test_ticks(void)
{
	int64_t interval;
	int error_pct;

	/*
	 * HOOK_SECOND must have been fired at least once when HOOK
	 * task starts. We only need to wait for just more than a second
	 * to allow it fires for the second time.
	 */
	crec_usleep(1300 * MSEC);

	interval = tick_time[1].val - tick_time[0].val;
	error_pct = (interval - HOOK_TICK_INTERVAL) * 100 / HOOK_TICK_INTERVAL;
	TEST_ASSERT_ABS_LESS(error_pct, 10);

	interval = second_time[1].val - second_time[0].val;
	error_pct = (interval - SECOND) * 100 / SECOND;
	TEST_ASSERT_ABS_LESS(error_pct, 10);

	return EC_SUCCESS;
}

static int test_priority(void)
{
	crec_usleep(HOOK_TICK_INTERVAL);
	TEST_ASSERT(tick_hook_count == tick2_hook_count);
	TEST_ASSERT(tick_hook_count == tick_count_seen_by_tick2);

	return EC_SUCCESS;
}

static int test_deferred(void)
{
	deferred_call_count = 0;
	hook_call_deferred(&deferred_func_data, 50 * MSEC);
	crec_usleep(100 * MSEC);
	TEST_ASSERT(deferred_call_count == 1);

	hook_call_deferred(&deferred_func_data, 50 * MSEC);
	crec_usleep(25 * MSEC);
	hook_call_deferred(&deferred_func_data, -1);
	crec_usleep(75 * MSEC);
	TEST_ASSERT(deferred_call_count == 1);

	hook_call_deferred(&deferred_func_data, 50 * MSEC);
	crec_usleep(25 * MSEC);
	hook_call_deferred(&deferred_func_data, -1);
	crec_usleep(15 * MSEC);
	hook_call_deferred(&deferred_func_data, 25 * MSEC);
	crec_usleep(50 * MSEC);
	TEST_ASSERT(deferred_call_count == 2);

	TEST_ASSERT(hook_call_deferred(&non_deferred_func_data, 50 * MSEC) !=
		    EC_SUCCESS);
	crec_usleep(100 * MSEC);
	TEST_ASSERT(deferred_call_count == 2);

	return EC_SUCCESS;
}

static int repeating_deferred_count;
static void deferred_repeating_func(void);
DECLARE_DEFERRED(deferred_repeating_func);

static void deferred_repeating_func(void)
{
	++repeating_deferred_count;

	crec_usleep(100 * MSEC);
	if (repeating_deferred_count < 5)
		hook_call_deferred(&deferred_repeating_func_data, SECOND);

	crec_usleep(100 * MSEC);
}

static int test_repeating_deferred(void)
{
	repeating_deferred_count = 0;
	hook_call_deferred(&deferred_repeating_func_data, 0);
	crec_usleep(MINUTE);
	TEST_EQ(repeating_deferred_count, 5, "%d");

	return EC_SUCCESS;
}

void run_test(int argc, const char **argv)
{
	test_reset();

	RUN_TEST(test_init_hook);
	RUN_TEST(test_init_early_hook);
	RUN_TEST(test_ticks);
	RUN_TEST(test_priority);
	RUN_TEST(test_deferred);
	RUN_TEST(test_repeating_deferred);

	test_print_result();
}
