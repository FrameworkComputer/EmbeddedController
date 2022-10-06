/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/ztest.h>
#include <console.h>

ZTEST_SUITE(console_cmd_power_button, NULL, NULL, NULL, NULL, NULL);

ZTEST_USER(console_cmd_power_button, test_return_ok)
{
	zassert_ok(shell_execute_cmd(get_ec_shell(), "powerbtn"), NULL);
}

ZTEST_USER(console_cmd_power_button, test_negative_delay)
{
	int rv;

	rv = shell_execute_cmd(get_ec_shell(), "powerbtn -1");

	zassert_not_equal(rv, EC_SUCCESS,
			  "Command should error on negative delay");
}

ZTEST_USER(console_cmd_power_button, test_invalid_arg)
{
	int rv;

	rv = shell_execute_cmd(get_ec_shell(), "powerbtn foo");

	zassert_equal(rv, EC_ERROR_PARAM1, "Expected %d, but got %d",
		      EC_ERROR_PARAM1, rv);
}
