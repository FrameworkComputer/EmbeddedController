/* Copyright 2022 The ChromiumOS Authors.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/shell/shell.h>
#include <zephyr/ztest.h>

#include "console.h"
#include "ec_commands.h"
#include "test/drivers/test_state.h"

ZTEST_SUITE(console_cmd_hibdelay, drivers_predicate_post_main, NULL, NULL, NULL,
	    NULL);

ZTEST_USER(console_cmd_hibdelay, test_too_many_args)
{
	zassert_ok(shell_execute_cmd(get_ec_shell(), "hibdelay 1 2"), NULL);
}

ZTEST_USER(console_cmd_hibdelay, test_no_args)
{
	zassert_ok(shell_execute_cmd(get_ec_shell(), "hibdelay"), NULL);
}

ZTEST_USER(console_cmd_hibdelay, test_invalid_arg)
{
	int rv = shell_execute_cmd(get_ec_shell(), "hibdelay 3.4");

	zassert_equal(rv, EC_ERROR_PARAM1, "Expected %d, but got %d",
		      EC_ERROR_PARAM1, rv);
}

ZTEST_USER(console_cmd_hibdelay, test_valid_args)
{
	zassert_ok(shell_execute_cmd(get_ec_shell(), "hibdelay 5"), NULL);
}
