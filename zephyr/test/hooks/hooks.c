/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <stdbool.h>
#include <ztest.h>

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

void test_main(void)
{
	ztest_test_suite(
		hooks_tests,
		ztest_unit_test(test_hook_list_multiple),
		ztest_unit_test(test_hook_list_single),
		ztest_unit_test(test_hook_list_empty),
		ztest_unit_test(test_deferred_func),
		ztest_unit_test(test_deferred_func_push_out),
		ztest_unit_test(test_deferred_func_cancel));

	ztest_run_test_suite(hooks_tests);
}
