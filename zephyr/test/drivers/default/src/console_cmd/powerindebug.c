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

ZTEST_USER(console_cmd_powerindebug, test_no_params)
{
	zassert_ok(shell_execute_cmd(get_ec_shell(), "powerindebug"),
		   "Failed to get debug mask");
}

ZTEST_USER(console_cmd_powerindebug, test_good_params)
{
	zassert_ok(shell_execute_cmd(get_ec_shell(), "powerindebug 0x10"),
		   "Failed to set debug mask");
	zassert_ok(shell_execute_cmd(get_ec_shell(), "powerindebug 0"),
		   "Failed to set debug mask");
}

ZTEST_USER(console_cmd_powerindebug, test_bad_params)
{
	int rv = shell_execute_cmd(get_ec_shell(), "powerindebug fish");

	zassert_equal(rv, EC_ERROR_PARAM1, "Expected %d, but got %d",
		      EC_ERROR_PARAM1, rv);
}

ZTEST_SUITE(console_cmd_powerindebug, NULL, NULL, NULL, NULL, NULL);
