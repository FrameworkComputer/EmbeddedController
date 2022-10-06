/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/shell/shell.h>
#include <zephyr/ztest.h>

#include "console.h"
#include "ec_commands.h"
#include "test/drivers/test_state.h"
#include "test/drivers/utils.h"

ZTEST_USER(console_cmd_button, test_button_no_arg)
{
	int rv;

	rv = shell_execute_cmd(get_ec_shell(), "button");

	zassert_equal(EC_ERROR_PARAM_COUNT, rv, "Expected %d, returned %d",
		      EC_ERROR_PARAM_COUNT, rv);
}

ZTEST_USER(console_cmd_button, test_button_vup)
{
	int rv;

	rv = shell_execute_cmd(get_ec_shell(), "button vup a");

	zassert_equal(EC_ERROR_PARAM2, rv, "Expected %d, returned %d",
		      EC_ERROR_PARAM2, rv);

	rv = shell_execute_cmd(get_ec_shell(), "button vup 50");

	zassert_ok(rv, "Expected %d, returned %d", EC_SUCCESS, rv);
}

ZTEST_USER(console_cmd_button, test_button_vdown)
{
	int rv;

	rv = shell_execute_cmd(get_ec_shell(), "button vdown a");

	zassert_equal(EC_ERROR_PARAM2, rv, "Expected %d, returned %d",
		      EC_ERROR_PARAM2, rv);

	rv = shell_execute_cmd(get_ec_shell(), "button vdown 50");

	zassert_ok(rv, "Expected %d, returned %d", EC_SUCCESS, rv);
}

ZTEST_USER(console_cmd_button, test_button_rec)
{
	int rv;

	rv = shell_execute_cmd(get_ec_shell(), "button rec 50");

	if (IS_ENABLED(CONFIG_DEDICATED_RECOVERY_BUTTON)) {
		zassert_ok(rv, "Expected %d, returned %d", EC_SUCCESS, rv);
	} else {
		/* Recovery button does not exist */
		zassert_equal(EC_ERROR_PARAM1, rv, "Expected %d, returned %d",
			      EC_ERROR_PARAM1, rv);
	}
}

ZTEST_SUITE(console_cmd_button, NULL, NULL, NULL, NULL, NULL);
