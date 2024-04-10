/* Copyright 2013 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Test hooks.
 */

#include "common.h"
#include "console.h"
#include "cts_common.h"
#include "hooks.h"
#include "task.h"
#include "timer.h"
#include "util.h"
#include "watchdog.h"

static int init_hook_count;
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

static void invalid_deferred_func(void)
{
	deferred_call_count++;
}

static const struct deferred_data invalid_deferred_func_data = {
	invalid_deferred_func
};

static enum cts_rc test_init_hook(void)
{
	if (init_hook_count != 1)
		return CTS_RC_FAILURE;
	return CTS_RC_SUCCESS;
}

static enum cts_rc test_ticks(void)
{
	int64_t interval;
	int error_pct;

	/*
	 * HOOK_SECOND must have been fired at least once when HOOK
	 * task starts. We only need to wait for just more than a second
	 * to allow it fires for the second time.
	 */
	crec_msleep(1300);

	interval = tick_time[1].val - tick_time[0].val;
	error_pct = (interval - HOOK_TICK_INTERVAL) * 100 / HOOK_TICK_INTERVAL;
	if (error_pct < -10 || 10 < error_pct) {
		CPRINTS("tick error=%d%% interval=%lld", error_pct, interval);
		return CTS_RC_FAILURE;
	}

	interval = second_time[1].val - second_time[0].val;
	error_pct = (interval - SECOND) * 100 / SECOND;
	if (error_pct < -10 || 10 < error_pct) {
		CPRINTS("second error=%d%% interval=%lld", error_pct, interval);
		return CTS_RC_FAILURE;
	}

	return CTS_RC_SUCCESS;
}

static enum cts_rc test_priority(void)
{
	crec_usleep(HOOK_TICK_INTERVAL);
	if (tick_hook_count != tick2_hook_count)
		return CTS_RC_FAILURE;
	if (tick_hook_count != tick_count_seen_by_tick2)
		return CTS_RC_FAILURE;
	return CTS_RC_SUCCESS;
}

static enum cts_rc test_deferred(void)
{
	deferred_call_count = 0;
	hook_call_deferred(&deferred_func_data, 50 * MSEC);
	if (deferred_call_count != 0) {
		CPRINTL("deferred_call_count=%d", deferred_call_count);
		return CTS_RC_FAILURE;
	}
	crec_msleep(100);
	if (deferred_call_count != 1) {
		CPRINTL("deferred_call_count=%d", deferred_call_count);
		return CTS_RC_FAILURE;
	}

	/* Test cancellation */
	deferred_call_count = 0;
	hook_call_deferred(&deferred_func_data, 50 * MSEC);
	crec_msleep(25);
	hook_call_deferred(&deferred_func_data, -1);
	crec_msleep(75);
	if (deferred_call_count != 0) {
		CPRINTL("deferred_call_count=%d", deferred_call_count);
		return CTS_RC_FAILURE;
	}

	/* Invalid deferred function */
	deferred_call_count = 0;
	if (hook_call_deferred(&invalid_deferred_func_data, 50 * MSEC) ==
	    EC_SUCCESS) {
		CPRINTL("non_deferred_func_data");
		return CTS_RC_FAILURE;
	}
	crec_msleep(100);
	if (deferred_call_count != 0) {
		CPRINTL("deferred_call_count=%d", deferred_call_count);
		return CTS_RC_FAILURE;
	}

	return CTS_RC_SUCCESS;
}

#include "cts_testlist.h"

void cts_task(void)
{
	cts_main_loop(tests, "Hook");
	task_wait_event(-1);
}
