/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "builtin/assert.h"
#include "console.h"
#include "test/drivers/test_mocks.h"
#include "test/drivers/test_state.h"

#include <zephyr/shell/shell.h>
#include <zephyr/ztest.h>

FAKE_VOID_FUNC(command_crash_nested_enable, int, const char **);
FAKE_VOID_FUNC(command_crash_nested_disable);

ZTEST_USER(console_cmd_crash, test_no_args)
{
	int rv = shell_execute_cmd(get_ec_shell(), "crash");

	zassert_equal(EC_ERROR_PARAM1, rv, "Expected %d, but got %d",
		      EC_ERROR_PARAM1, rv);
	zassert_equal(0, command_crash_nested_enable_fake.call_count);
	zassert_equal(0, command_crash_nested_disable_fake.call_count);
}

ZTEST_USER(console_cmd_crash, test_bad_arg)
{
	int rv;

	rv = shell_execute_cmd(get_ec_shell(), "crash foo");

	zassert_equal(EC_ERROR_PARAM1, rv, NULL);
	zassert_equal(0, command_crash_nested_enable_fake.call_count);
	zassert_equal(1, command_crash_nested_disable_fake.call_count);
}

ZTEST_USER(console_cmd_crash, test_assert)
{
	int rv;

	RESET_FAKE(assert_post_action);
	rv = shell_execute_cmd(get_ec_shell(), "crash assert");

	zassert_equal(EC_ERROR_UNKNOWN, rv, NULL);
	zassert_equal(0, command_crash_nested_enable_fake.call_count);
	zassert_equal(1, command_crash_nested_disable_fake.call_count);
}

ZTEST_USER(console_cmd_crash, test_assert_assert)
{
	int rv;

	rv = shell_execute_cmd(get_ec_shell(), "crash assert asset");

	zassert_equal(EC_ERROR_UNKNOWN, rv, NULL);
	zassert_equal(1, command_crash_nested_enable_fake.call_count);
	zassert_equal(3, command_crash_nested_enable_fake.arg0_val);
	zassert_equal(1, command_crash_nested_disable_fake.call_count);
}

static void reset(void *data)
{
	ARG_UNUSED(data);

	RESET_FAKE(command_crash_nested_enable);
	RESET_FAKE(command_crash_nested_disable);
}

ZTEST_SUITE(console_cmd_crash, drivers_predicate_post_main, NULL, reset, NULL,
	    NULL);
