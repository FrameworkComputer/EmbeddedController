/* Copyright 2022 The ChromiumOS Authors.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/shell/shell.h>
#include <zephyr/ztest.h>

#include "console.h"
#include "ec_commands.h"
#include "test/drivers/test_state.h"
#include "test/drivers/utils.h"

/* Default battery command */
ZTEST_USER(console_cmd_battery, test_battery_default)
{
	zassert_ok(shell_execute_cmd(get_ec_shell(), "battery"),
		   "Failed default print");
}

/* Battery command with repeat */
ZTEST_USER(console_cmd_battery, test_battery_repeat)
{
	zassert_ok(shell_execute_cmd(get_ec_shell(), "battery 2"),
		   "Failed default print");
	zassert_ok(shell_execute_cmd(get_ec_shell(), "battery 8"),
		   "Failed default print");
}

/* Battery command with repeat and sleep */
ZTEST_USER(console_cmd_battery, test_battery_repeat_sleep)
{
	zassert_ok(shell_execute_cmd(get_ec_shell(), "battery 2 400"),
		   "Failed default print");
	zassert_ok(shell_execute_cmd(get_ec_shell(), "battery 8 200"),
		   "Failed default print");
}

/* Battery command with invalid repeat and sleep */
ZTEST_USER(console_cmd_battery, test_battery_bad_repeat_sleep)
{
	int rv = shell_execute_cmd(get_ec_shell(), "battery fish 400");

	zassert_equal(rv, EC_ERROR_INVAL, "Expected %d, but got %d",
		      EC_ERROR_INVAL, rv);

	rv = shell_execute_cmd(get_ec_shell(), "battery 2 fish");

	zassert_equal(rv, EC_ERROR_INVAL, "Expected %d, but got %d",
		      EC_ERROR_INVAL, rv);
}

ZTEST_SUITE(console_cmd_battery, NULL, NULL, NULL, NULL, NULL);
