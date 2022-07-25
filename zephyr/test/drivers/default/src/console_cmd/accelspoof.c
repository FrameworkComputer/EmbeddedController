/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/shell/shell.h>
#include <ztest.h>

#include "console.h"
#include "ec_commands.h"
#include "motion_sense.h"
#include "test/drivers/test_state.h"

static void console_cmd_accelspoof_after(void *fixture)
{
	ARG_UNUSED(fixture);
	shell_execute_cmd(get_ec_shell(), "accelspoof 0 off");
	motion_sensors[0].spoof_xyz[0] = 0;
	motion_sensors[0].spoof_xyz[1] = 0;
	motion_sensors[0].spoof_xyz[2] = 0;
}

ZTEST_SUITE(console_cmd_accelspoof, drivers_predicate_post_main, NULL, NULL,
	    console_cmd_accelspoof_after, NULL);

ZTEST_USER(console_cmd_accelspoof, test_too_few_args)
{
	int rv = shell_execute_cmd(get_ec_shell(), "accelspoof");

	zassert_equal(rv, EC_ERROR_PARAM_COUNT, "Expected %d, but got %d",
		      EC_ERROR_PARAM_COUNT, rv);
}

ZTEST_USER(console_cmd_accelspoof, test_invalid_sensor_id)
{
	int rv;

	rv = shell_execute_cmd(get_ec_shell(), "accelspoof -1");
	zassert_equal(rv, EC_ERROR_PARAM1, "Expected %d, but got %d",
		      EC_ERROR_PARAM1, rv);

	rv = shell_execute_cmd(get_ec_shell(), "accelspoof 100");
	zassert_equal(rv, EC_ERROR_PARAM1, "Expected %d, but got %d",
		      EC_ERROR_PARAM1, rv);
}

ZTEST_USER(console_cmd_accelspoof, test_print_mode)
{
	zassert_ok(shell_execute_cmd(get_ec_shell(), "accelspoof 0"), NULL);
}

ZTEST_USER(console_cmd_accelspoof, test_invalid_boolean)
{
	int rv = shell_execute_cmd(get_ec_shell(), "accelspoof 0 bar");

	zassert_equal(rv, EC_ERROR_PARAM2, "Expected %d, but got %d",
		      EC_ERROR_PARAM2, rv);
}

ZTEST_USER(console_cmd_accelspoof, test_enable_disable)
{
	zassert_ok(shell_execute_cmd(get_ec_shell(), "accelspoof 0 on"), NULL);
	zassert_true(motion_sensors[0].flags & MOTIONSENSE_FLAG_IN_SPOOF_MODE,
		     NULL);

	zassert_ok(shell_execute_cmd(get_ec_shell(), "accelspoof 0 off"), NULL);
	zassert_false(motion_sensors[0].flags & MOTIONSENSE_FLAG_IN_SPOOF_MODE,
		      NULL);
}

ZTEST_USER(console_cmd_accelspoof, test_wrong_num_axis_args)
{
	int rv = shell_execute_cmd(get_ec_shell(), "accelspoof 0 on 1");

	zassert_equal(rv, EC_ERROR_PARAM_COUNT, "Expected %d, but got %d",
		      EC_ERROR_PARAM_COUNT, rv);
}

ZTEST_USER(console_cmd_accelspoof, test_enable_explicit_values)
{
	zassert_ok(shell_execute_cmd(get_ec_shell(), "accelspoof 0 on 1 2 3"),
		   NULL);
	zassert_equal(1, motion_sensors[0].spoof_xyz[0], NULL);
	zassert_equal(2, motion_sensors[0].spoof_xyz[1], NULL);
	zassert_equal(3, motion_sensors[0].spoof_xyz[2], NULL);
}

ZTEST_USER(console_cmd_accelspoof, test_enable_implicit_values)
{
	motion_sensors[0].raw_xyz[0] = 4;
	motion_sensors[0].raw_xyz[1] = 5;
	motion_sensors[0].raw_xyz[2] = 6;
	zassert_ok(shell_execute_cmd(get_ec_shell(), "accelspoof 0 on"), NULL);
	zassert_equal(4, motion_sensors[0].spoof_xyz[0], NULL);
	zassert_equal(5, motion_sensors[0].spoof_xyz[1], NULL);
	zassert_equal(6, motion_sensors[0].spoof_xyz[2], NULL);
}
