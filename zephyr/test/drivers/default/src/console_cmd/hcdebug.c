/* Copyright 2022 The ChromiumOS Authors.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/shell/shell.h>
#include <zephyr/ztest.h>

#include "console.h"
#include "ec_commands.h"
#include "test/drivers/test_state.h"

static void console_cmd_hcdebug_after(void *fixture)
{
	ARG_UNUSED(fixture);
	shell_execute_cmd(get_ec_shell(), "hcdebug off");
}

ZTEST_SUITE(console_cmd_hcdebug, drivers_predicate_post_main, NULL, NULL,
	    console_cmd_hcdebug_after, NULL);

ZTEST_USER(console_cmd_hcdebug, test_too_many_args)
{
	int rv = shell_execute_cmd(get_ec_shell(), "hcdebug arg1 arg2");

	zassert_not_equal(rv, EC_SUCCESS, "Expected %d, but got %d",
			  EC_ERROR_PARAM_COUNT, rv);
}

ZTEST_USER(console_cmd_hcdebug, test_no_args)
{
	zassert_ok(shell_execute_cmd(get_ec_shell(), "hcdebug"), NULL);
}

ZTEST_USER(console_cmd_hcdebug, test_invalid_arg)
{
	int rv = shell_execute_cmd(get_ec_shell(), "hcdebug bar");

	zassert_equal(rv, EC_ERROR_PARAM1, "Expected %d, but got %d",
		      EC_ERROR_PARAM1, rv);
}

ZTEST_USER(console_cmd_hcdebug, test_valid_args)
{
	zassert_ok(shell_execute_cmd(get_ec_shell(), "hcdebug off"), NULL);
	zassert_ok(shell_execute_cmd(get_ec_shell(), "hcdebug normal"), NULL);
	zassert_ok(shell_execute_cmd(get_ec_shell(), "hcdebug every"), NULL);
	zassert_ok(shell_execute_cmd(get_ec_shell(), "hcdebug params"), NULL);
}
