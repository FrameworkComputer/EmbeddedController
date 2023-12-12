/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "console.h"
#include "ec_commands.h"
#include "test/drivers/test_state.h"
#include "test/drivers/utils.h"

#include <zephyr/shell/shell.h>
#include <zephyr/ztest.h>

#if defined(SECTION_IS_RW)

ZTEST_USER(console_cmd_sleeptimeout, test_no_params)
{
	zassert_ok(shell_execute_cmd(get_ec_shell(), "sleeptimeout"),
		   "Failed default print");
}

ZTEST_USER(console_cmd_sleeptimeout, test_good_params)
{
	zassert_ok(shell_execute_cmd(get_ec_shell(), "sleeptimeout default"),
		   "Failed to set default sleep timeout");
	zassert_ok(shell_execute_cmd(get_ec_shell(), "sleeptimeout infinite"),
		   "Failed to disable sleep timeout");
	zassert_ok(shell_execute_cmd(get_ec_shell(), "sleeptimeout 1500"),
		   "Failed to set sleep timeout to a custom value");
}

ZTEST_USER(console_cmd_sleeptimeout, test_bad_params)
{
	int rv = shell_execute_cmd(get_ec_shell(), "sleeptimeout 0");

	zassert_equal(rv, EC_ERROR_PARAM1, "Expected %d, but got %d",
		      EC_ERROR_PARAM1, rv);

	rv = shell_execute_cmd(get_ec_shell(),
			       "sleeptimeout EC_HOST_SLEEP_TIMEOUT_INFINITE");

	zassert_equal(rv, EC_ERROR_PARAM1, "Expected %d, but got %d",
		      EC_ERROR_PARAM1, rv);
}

ZTEST_SUITE(console_cmd_sleeptimeout, NULL, NULL, NULL, NULL, NULL);

#endif /* SECTION_IS_RW */
