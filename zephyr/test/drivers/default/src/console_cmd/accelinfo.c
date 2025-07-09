/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "config.h"
#include "console.h"
#include "ec_commands.h"
#include "test/drivers/test_state.h"
#include "timer.h"

#include <zephyr/shell/shell.h>
#include <zephyr/ztest.h>

static void console_cmd_accelinfo_after(void *fixture)
{
	ARG_UNUSED(fixture);
	shell_execute_cmd(get_ec_shell(), "accelinfo off");
}

ZTEST_SUITE(console_cmd_accelinfo, drivers_predicate_post_main, NULL, NULL,
	    console_cmd_accelinfo_after, NULL);

ZTEST_USER(console_cmd_accelinfo, test_too_many_args)
{
	int rv = shell_execute_cmd(get_ec_shell(), "accelinfo arg1 arg2");

	zassert_equal(rv, EC_ERROR_PARAM_COUNT, "Expected %d, but got %d",
		      EC_ERROR_PARAM_COUNT, rv);
}

ZTEST_USER(console_cmd_accelinfo, test_print_once)
{
	zassert_ok(shell_execute_cmd(get_ec_shell(), "accelinfo"));
}

ZTEST_USER(console_cmd_accelinfo, test_invalid_arg)
{
	int rv = shell_execute_cmd(get_ec_shell(), "accelinfo bar");

	zassert_equal(rv, EC_ERROR_PARAM1, "Expected %d, but got %d",
		      EC_ERROR_PARAM1, rv);
}

ZTEST_USER(console_cmd_accelinfo, test_enable_disable)
{
	/*
	 * There's no way to verify what is being printed to the console yet, so
	 * just assert that the command executed and returned 0.
	 */
	zassert_ok(shell_execute_cmd(get_ec_shell(), "accelinfo on"));
	k_msleep(CONFIG_MOTION_MIN_SENSE_WAIT_TIME * USEC_PER_MSEC * 2);

	zassert_ok(shell_execute_cmd(get_ec_shell(), "accelinfo off"));
}
