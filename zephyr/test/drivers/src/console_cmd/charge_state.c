/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <shell/shell.h>
#include <ztest.h>

#include "console.h"
#include "test_state.h"

ZTEST_USER(console_cmd_charge_state, test_idle_too_few_args)
{
	int rv;

	rv = shell_execute_cmd(get_ec_shell(), "chgstate idle");
	zassert_equal(rv, EC_ERROR_PARAM_COUNT, "Expected %d, but got %d",
		      EC_ERROR_PARAM_COUNT, rv);
}

ZTEST_USER(console_cmd_charge_state, test_idle_arg_not_a_bool)
{
	int rv;

	/*
	 * There are many strings that will fail parse_bool(), just test one to
	 * test the code path in the command, other tests for parse_bool are
	 * done in the respective unit test.
	 */
	rv = shell_execute_cmd(get_ec_shell(), "chgstate idle g");
	zassert_equal(rv, EC_ERROR_PARAM2, "Expected %d, but got %d",
		      EC_ERROR_PARAM2, rv);
}

ZTEST_USER(console_cmd_charge_state, test_discharge_too_few_args)
{
	int rv;

	rv = shell_execute_cmd(get_ec_shell(), "chgstate discharge");
	zassert_equal(rv, EC_ERROR_PARAM_COUNT, "Expected %d, but got %d",
		      EC_ERROR_PARAM_COUNT, rv);
}

ZTEST_USER(console_cmd_charge_state, test_discharge_arg_not_a_bool)
{
	int rv;

	/*
	 * There are many strings that will fail parse_bool(), just test one to
	 * test the code path in the command, other tests for parse_bool are
	 * done in the respective unit test.
	 */
	rv = shell_execute_cmd(get_ec_shell(), "chgstate discharge g");
	zassert_equal(rv, EC_ERROR_PARAM2, "Expected %d, but got %d",
		      EC_ERROR_PARAM2, rv);
}

ZTEST_USER(console_cmd_charge_state, test_sustain_too_few_args__2_args)
{
	int rv;

	rv = shell_execute_cmd(get_ec_shell(), "chgstate sustain");
	zassert_equal(rv, EC_ERROR_PARAM_COUNT, "Expected %d, but got %d",
		      EC_ERROR_PARAM_COUNT, rv);
}

ZTEST_USER(console_cmd_charge_state, test_sustain_too_few_args__3_args)
{
	int rv;

	rv = shell_execute_cmd(get_ec_shell(), "chgstate sustain 5");
	zassert_equal(rv, EC_ERROR_PARAM_COUNT, "Expected %d, but got %d",
		      EC_ERROR_PARAM_COUNT, rv);
}

ZTEST_SUITE(console_cmd_charge_state, drivers_predicate_post_main, NULL, NULL,
	    NULL, NULL);
