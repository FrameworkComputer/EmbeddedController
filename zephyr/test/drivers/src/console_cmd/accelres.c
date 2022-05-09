/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <fff.h>
#include <zephyr/shell/shell.h>
#include <ztest.h>

#include "accelgyro.h"
#include "console.h"
#include "driver/accel_bma2x2.h"
#include "ec_commands.h"
#include "motion_sense.h"
#include "test/drivers/test_state.h"

FAKE_VALUE_FUNC(int, set_resolution, const struct motion_sensor_t *, int, int);

struct console_cmd_accelres_fixture {
	const struct accelgyro_drv *sensor_0_drv;
	struct accelgyro_drv mock_drv;
};

void *console_cmd_accelres_setup(void)
{
	static struct console_cmd_accelres_fixture fixture = {
		.mock_drv = {
			.set_resolution = set_resolution,
		},
	};

	fixture.sensor_0_drv = motion_sensors[0].drv;

	return &fixture;
}

void console_cmd_accelres_before(void *fixture)
{
	ARG_UNUSED(fixture);
	RESET_FAKE(set_resolution);
	FFF_RESET_HISTORY();
}

void console_cmd_accelres_after(void *fixture)
{
	struct console_cmd_accelres_fixture *this = fixture;

	motion_sensors[0].drv = this->sensor_0_drv;
}

ZTEST_SUITE(console_cmd_accelres, drivers_predicate_post_main,
	    console_cmd_accelres_setup, NULL, console_cmd_accelres_after, NULL);

ZTEST_USER(console_cmd_accelres, test_too_few_args)
{
	int rv = shell_execute_cmd(get_ec_shell(), "accelres");

	zassert_equal(EC_ERROR_PARAM_COUNT, rv, "Expected %d, but got %d",
		      EC_ERROR_PARAM_COUNT, rv);
}

ZTEST_USER(console_cmd_accelres, test_too_many_args)
{
	int rv = shell_execute_cmd(get_ec_shell(), "accelres 1 2 3 4");

	zassert_equal(EC_ERROR_PARAM_COUNT, rv, "Expected %d, but got %d",
		      EC_ERROR_PARAM_COUNT, rv);
}

ZTEST_USER(console_cmd_accelres, test_invalid_sensor_num)
{
	int rv;

	rv = shell_execute_cmd(get_ec_shell(), "accelres f");
	zassert_equal(EC_ERROR_PARAM1, rv, "Expected %d, but got %d",
		      EC_ERROR_PARAM1, rv);

	rv = shell_execute_cmd(get_ec_shell(), "accelres -1");
	zassert_equal(EC_ERROR_PARAM1, rv, "Expected %d, but got %d",
		      EC_ERROR_PARAM1, rv);

	rv = shell_execute_cmd(get_ec_shell(), "accelres 100");
	zassert_equal(EC_ERROR_PARAM1, rv, "Expected %d, but got %d",
		      EC_ERROR_PARAM1, rv);
}

ZTEST_USER(console_cmd_accelres, test_print_res)
{
	zassert_ok(shell_execute_cmd(get_ec_shell(), "accelres 0"), NULL);
}

ZTEST_USER(console_cmd_accelres, test_set_res__invalid_data)
{
	int rv = shell_execute_cmd(get_ec_shell(), "accelres 0 f");

	zassert_equal(EC_ERROR_PARAM2, rv, "Expected %d, but got %d",
		      EC_ERROR_PARAM2, rv);
}

ZTEST_USER(console_cmd_accelres, test_set_res__no_setter)
{
	int resolution;

	zassert_ok(shell_execute_cmd(get_ec_shell(), "accelres 0 4"), NULL);
	resolution = motion_sensors[0].drv->get_resolution(&motion_sensors[0]);
	zassert_equal(BMA2x2_RESOLUTION, resolution, "Expected %d, but got %d",
		      BMA2x2_RESOLUTION, resolution);
}

ZTEST_USER_F(console_cmd_accelres, test_set_res__bad_res_value)
{
	int rv;

	set_resolution_fake.return_val = EC_ERROR_INVAL;
	motion_sensors[0].drv = &this->mock_drv;
	rv = shell_execute_cmd(get_ec_shell(), "accelres 0 0");
	zassert_equal(EC_ERROR_PARAM2, rv, "Expected %d, but got %d",
		      EC_ERROR_PARAM2, rv);
}

ZTEST_USER(console_cmd_accelres, test_invalid_rounding_arg)
{
	int rv = shell_execute_cmd(get_ec_shell(), "accelres 0 12 f");

	zassert_equal(EC_ERROR_PARAM3, rv, "Expected %d, but got %d",
		      EC_ERROR_PARAM3, rv);
}
