/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <fff.h>
#include <shell/shell.h>
#include <ztest.h>

#include "accelgyro.h"
#include "console.h"
#include "motion_sense.h"
#include "test/drivers/test_state.h"
#include "test/drivers/utils.h"

FAKE_VALUE_FUNC(int, mock_init, struct motion_sensor_t *);

struct console_cmd_accelinit_fixture {
	const struct accelgyro_drv *sensor_0_drv;
	struct accelgyro_drv mock_drv;
};

static void *console_cmd_accelinit_setup(void)
{
	static struct console_cmd_accelinit_fixture fixture = {
		.mock_drv = {
			.init = mock_init,
		},
	};
	fixture.sensor_0_drv = motion_sensors[0].drv;

	return &fixture;
}

static void console_cmd_accelinit_before(void *fixture)
{
	ARG_UNUSED(fixture);
	RESET_FAKE(mock_init);
	FFF_RESET_HISTORY();
}
static void console_cmd_accelinit_after(void *fixture)
{
	struct console_cmd_accelinit_fixture *this = fixture;

	motion_sensors[0].drv = this->sensor_0_drv;
	motion_sensors[0].drv->init(&motion_sensors[0]);
}

ZTEST_SUITE(console_cmd_accelinit, drivers_predicate_post_main,
	    console_cmd_accelinit_setup, console_cmd_accelinit_before,
	    console_cmd_accelinit_after, NULL);

ZTEST_USER(console_cmd_accelinit, test_invalid_sensor_num)
{
	int rv;

	rv = shell_execute_cmd(get_ec_shell(), "accelinit f");
	zassert_equal(EC_ERROR_PARAM1, rv, "Expected %d, but got %d",
		      EC_ERROR_PARAM1, rv);

	rv = shell_execute_cmd(get_ec_shell(), "accelinit -1");
	zassert_equal(EC_ERROR_PARAM1, rv, "Expected %d, but got %d",
		      EC_ERROR_PARAM1, rv);

	rv = shell_execute_cmd(get_ec_shell(), "accelinit 100");
	zassert_equal(EC_ERROR_PARAM1, rv, "Expected %d, but got %d",
		      EC_ERROR_PARAM1, rv);
}

ZTEST_USER(console_cmd_accelinit, test_state_was_set)
{
	motion_sensors[0].state = SENSOR_INIT_ERROR;

	zassert_ok(shell_execute_cmd(get_ec_shell(), "accelinit 0"), NULL);
	zassert_equal(SENSOR_INITIALIZED, motion_sensors[0].state,
		      "Expected %d, but got %d", SENSOR_INITIALIZED,
		      motion_sensors[0].state);
}

ZTEST_USER_F(console_cmd_accelinit, test_fail_3_times)
{
	mock_init_fake.return_val = 1;
	motion_sensors[0].drv = &this->mock_drv;
	motion_sensors[0].state = SENSOR_INITIALIZED;

	zassert_ok(shell_execute_cmd(get_ec_shell(), "accelinit 0"), NULL);
	zassert_equal(3, mock_init_fake.call_count,
		      "Expected 3 calls, but got %d",
		      mock_init_fake.call_count);
	zassert_equal(SENSOR_INIT_ERROR, motion_sensors[0].state,
		      "Expected %d, but got %d", SENSOR_INIT_ERROR,
		      motion_sensors[0].state);
}
