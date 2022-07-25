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
#include "test/drivers/utils.h"

static int original_sensor_0_s0_config_odr;

static void *console_cmd_accelrate_setup(void)
{
	original_sensor_0_s0_config_odr =
		motion_sensors[0].config[SENSOR_CONFIG_EC_S0].odr;
	return NULL;
}

static void console_cmd_accelrate_after(void *state)
{
	ARG_UNUSED(state);
	motion_sensors[0].config[SENSOR_CONFIG_EC_S0].odr =
		original_sensor_0_s0_config_odr;
}

ZTEST_SUITE(console_cmd_accelrate, drivers_predicate_post_main,
	    console_cmd_accelrate_setup, NULL, console_cmd_accelrate_after,
	    NULL);

ZTEST_USER(console_cmd_accelrate, test_bad_arg_count)
{
	int rv;

	rv = shell_execute_cmd(get_ec_shell(), "accelrate");
	zassert_equal(EC_ERROR_PARAM_COUNT, rv, "Expected %d, but got %d",
		      EC_ERROR_PARAM_COUNT, rv);

	rv = shell_execute_cmd(get_ec_shell(), "accelrate 1 2 3 4");
	zassert_equal(EC_ERROR_PARAM_COUNT, rv, "Expected %d, but got %d",
		      EC_ERROR_PARAM_COUNT, rv);
}

ZTEST_USER(console_cmd_accelrate, test_invalid_sensor_num)
{
	int rv;

	rv = shell_execute_cmd(get_ec_shell(), "accelrate f");
	zassert_equal(EC_ERROR_PARAM1, rv, "Expected %d, but got %d",
		      EC_ERROR_PARAM1, rv);

	rv = shell_execute_cmd(get_ec_shell(), "accelrate -1");
	zassert_equal(EC_ERROR_PARAM1, rv, "Expected %d, but got %d",
		      EC_ERROR_PARAM1, rv);

	rv = shell_execute_cmd(get_ec_shell(), "accelrate 100");
	zassert_equal(EC_ERROR_PARAM1, rv, "Expected %d, but got %d",
		      EC_ERROR_PARAM1, rv);
}

ZTEST_USER(console_cmd_accelrate, test_print_rate)
{
	zassert_ok(shell_execute_cmd(get_ec_shell(), "accelrate 0"), NULL);
}

ZTEST_USER(console_cmd_accelrate, test_bad_rate_value)
{
	int rv = shell_execute_cmd(get_ec_shell(), "accelrate 0 f");

	zassert_equal(EC_ERROR_PARAM2, rv, "Expected %d, but got %d",
		      EC_ERROR_PARAM2, rv);
}

ZTEST_USER(console_cmd_accelrate, test_set_ap_rate)
{
	test_set_chipset_to_s0();

	motion_sensors[0].config[SENSOR_CONFIG_EC_S0].odr = 0;
	zassert_ok(shell_execute_cmd(get_ec_shell(), "accelrate 0 100"), NULL);
	zassert_equal(100 | ROUND_UP_FLAG,
		      motion_sensors[0].config[SENSOR_CONFIG_EC_S0].odr,
		      "Expected %d, but got %d", 100 | ROUND_UP_FLAG,
		      motion_sensors[0].config[SENSOR_CONFIG_EC_S0].odr);

	/* Try explicit round up value: 1 */
	motion_sensors[0].config[SENSOR_CONFIG_EC_S0].odr = 0;
	zassert_ok(shell_execute_cmd(get_ec_shell(), "accelrate 0 100 1"),
		   NULL);
	zassert_equal(100 | ROUND_UP_FLAG,
		      motion_sensors[0].config[SENSOR_CONFIG_EC_S0].odr,
		      "Expected %d, but got %d", 100 | ROUND_UP_FLAG,
		      motion_sensors[0].config[SENSOR_CONFIG_EC_S0].odr);

	motion_sensors[0].config[SENSOR_CONFIG_EC_S0].odr = 0;
	zassert_ok(shell_execute_cmd(get_ec_shell(), "accelrate 0 100 0"),
		   NULL);
	zassert_equal(100, motion_sensors[0].config[SENSOR_CONFIG_EC_S0].odr,
		      "Expected %d, but got %d", 100 | ROUND_UP_FLAG,
		      motion_sensors[0].config[SENSOR_CONFIG_EC_S0].odr);
}
