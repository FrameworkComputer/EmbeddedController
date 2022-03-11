/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <stdbool.h>
#include <ztest.h>

#include "ap_power/ap_power.h"
#include "hooks.h"

static bool h1_called;
static bool h2_called;
static bool h3_called;

static void h1(void)
{
	zassert_false(h1_called, "h1 was called, but should not have been");
	zassert_false(h2_called, "h2 was called, but should not have been");
	zassert_false(h3_called, "h3 was called, but should not have been");
	h1_called = true;
}
DECLARE_HOOK(HOOK_TEST_1, h1, HOOK_PRIO_FIRST);

static void h2(void)
{
	zassert_true(h1_called, "h1 was not called, but should have been");
	zassert_false(h2_called, "h2 was called, but should not have been");
	zassert_false(h3_called, "h3 was called, but should not have been");
	h2_called = true;
}
DECLARE_HOOK(HOOK_TEST_1, h2, HOOK_PRIO_DEFAULT);

static void h3(void)
{
	zassert_true(h1_called, "h1 was not called, but should have been");
	zassert_true(h2_called, "h2 was not called, but should have been");
	zassert_false(h3_called, "h3 was called, but should not have been");
	h3_called = true;
}
DECLARE_HOOK(HOOK_TEST_1, h3, HOOK_PRIO_LAST);

static void test_hook_list_multiple(void)
{
	hook_notify(HOOK_TEST_1);
	zassert_true(h1_called, "h1 was not called, but should have been");
	zassert_true(h2_called, "h2 was not called, but should have been");
	zassert_true(h3_called, "h3 was not called, but should have been");
}

static bool h4_called;

static void h4(void)
{
	zassert_false(h4_called, "h4 was called, but should not have been");
	h4_called = true;
}
DECLARE_HOOK(HOOK_TEST_2, h4, HOOK_PRIO_DEFAULT);

static void test_hook_list_single(void)
{
	hook_notify(HOOK_TEST_2);
	zassert_true(h4_called, "h4 was not called, but should have been");
}

static void test_hook_list_empty(void)
{
	hook_notify(HOOK_TEST_3);
}

static bool deferred_func_called;

#define DEFERRED_DELAY_US (500 * 1000)
static void deferred_func(void)
{
	deferred_func_called = true;
}
DECLARE_DEFERRED(deferred_func);

static void test_deferred_func(void)
{
	zassert_false(
		deferred_func_called,
		"The deferred function was called, but should not have been");
	hook_call_deferred(&deferred_func_data, DEFERRED_DELAY_US);
	zassert_false(
		deferred_func_called,
		"The deferred function was called, but should not have been");
	k_usleep(DEFERRED_DELAY_US * 2);
	zassert_true(
		deferred_func_called,
		"The deferred function was not called, but should have been");
}

static bool deferred_func_2_called;

static void deferred_func_2(void)
{
	deferred_func_2_called = true;
}
DECLARE_DEFERRED(deferred_func_2);

/*
 * Test that repeated calls to hook_call_deferred result in the
 * function being pushed out.
 */
static void test_deferred_func_push_out(void)
{
	zassert_false(
		deferred_func_2_called,
		"The deferred function was called, but should not have been");
	hook_call_deferred(&deferred_func_2_data, DEFERRED_DELAY_US);
	hook_call_deferred(&deferred_func_2_data, DEFERRED_DELAY_US * 3);
	k_usleep(DEFERRED_DELAY_US * 2);
	zassert_false(
		deferred_func_2_called,
		"The deferred function was called, but should not have been");
	k_usleep(DEFERRED_DELAY_US * 2);
	zassert_true(
		deferred_func_called,
		"The deferred function was not called, but should have been");
}

static bool deferred_func_3_called;

static void deferred_func_3(void)
{
	deferred_func_3_called = true;
}
DECLARE_DEFERRED(deferred_func_3);

static void test_deferred_func_cancel(void)
{
	zassert_false(
		deferred_func_3_called,
		"The deferred function was called, but should not have been");
	hook_call_deferred(&deferred_func_3_data, DEFERRED_DELAY_US);
	hook_call_deferred(&deferred_func_3_data, -1);
	k_usleep(DEFERRED_DELAY_US * 2);
	zassert_false(
		deferred_func_3_called,
		"The deferred function was called, but should not have been");
}

/*
 * Structure passed to event listeners.
 */
struct events {
	struct ap_power_ev_callback cb;
	enum ap_power_events event;
	int count;
};

/*
 * Common handler.
 * Increment count, and store event received.
 */
static void ev_handler(struct ap_power_ev_callback *callback,
		       struct ap_power_ev_data data)
{
	struct events *ev = CONTAINER_OF(callback, struct events, cb);

	ev->count++;
	ev->event = data.event;
}

static void test_hook_ap_power_events(void)
{
	static struct events cb;

	ap_power_ev_init_callback(&cb.cb, ev_handler, AP_POWER_SUSPEND);
	ap_power_ev_add_callback(&cb.cb);
	hook_notify(HOOK_CHIPSET_SUSPEND);
	zassert_equal(1, cb.count, "Callback not called");
	zassert_equal(AP_POWER_SUSPEND, cb.event, "Wrong event");
	ap_power_ev_remove_callback(&cb.cb);
	hook_notify(HOOK_CHIPSET_SUSPEND);
	zassert_equal(1, cb.count, "Callback called");

	cb.count = 0;
	ap_power_ev_init_callback(&cb.cb, ev_handler,
		AP_POWER_SUSPEND|AP_POWER_RESUME);
	ap_power_ev_add_callback(&cb.cb);
	hook_notify(HOOK_CHIPSET_SUSPEND);
	zassert_equal(1, cb.count, "Callbacks not called");
	zassert_equal(AP_POWER_SUSPEND, cb.event, "Wrong event");
	hook_notify(HOOK_CHIPSET_RESUME);
	zassert_equal(2, cb.count, "Callbacks not called");
	zassert_equal(AP_POWER_RESUME, cb.event, "Wrong event");

	ap_power_ev_remove_events(&cb.cb, AP_POWER_SUSPEND);
	hook_notify(HOOK_CHIPSET_SUSPEND);
	zassert_equal(2, cb.count, "Suspend allback called");

	hook_notify(HOOK_CHIPSET_STARTUP);
	zassert_equal(2, cb.count, "Startup callback called");
	ap_power_ev_add_events(&cb.cb, AP_POWER_STARTUP);
	hook_notify(HOOK_CHIPSET_STARTUP);
	zassert_equal(3, cb.count, "Startup callback not called");
}

void test_main(void)
{
	ztest_test_suite(
		hooks_tests,
		ztest_unit_test(test_hook_list_multiple),
		ztest_unit_test(test_hook_list_single),
		ztest_unit_test(test_hook_list_empty),
		ztest_unit_test(test_deferred_func),
		ztest_unit_test(test_deferred_func_push_out),
		ztest_unit_test(test_deferred_func_cancel),
		ztest_unit_test(test_hook_ap_power_events));

	ztest_run_test_suite(hooks_tests);
}
