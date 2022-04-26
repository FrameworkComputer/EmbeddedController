/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <fff.h>
#include <shell/shell.h>
#include <ztest.h>

#include "accelgyro.h"
#include "console.h"
#include "ec_commands.h"
#include "motion_sense.h"
#include "test/drivers/test_state.h"
#include "test/drivers/utils.h"

FAKE_VALUE_FUNC(int, mock_read, const struct motion_sensor_t *, int *);
FAKE_VALUE_FUNC(int, mock_set_data_rate, const struct motion_sensor_t *, int,
		int);
FAKE_VALUE_FUNC(int, mock_get_data_rate, const struct motion_sensor_t *);

struct console_cmd_accelread_fixture {
	const struct accelgyro_drv *sensor_0_drv;
	struct accelgyro_drv mock_drv;
};

static void *console_cmd_accelread_setup(void)
{
	static struct console_cmd_accelread_fixture fixture = {
		.mock_drv = {
			.read = mock_read,
			/*
			 * Data rate functions are required so that motion_sense
			 * task doesn't segfault.
			 */
			.set_data_rate = mock_set_data_rate,
			.get_data_rate = mock_get_data_rate,
		},
	};
	fixture.sensor_0_drv = motion_sensors[0].drv;

	return &fixture;
}

static void console_cmd_accelread_before(void *fixture)
{
	ARG_UNUSED(fixture);
	RESET_FAKE(mock_read);
	RESET_FAKE(mock_set_data_rate);
	RESET_FAKE(mock_get_data_rate);
	FFF_RESET_HISTORY();
}

static void console_cmd_accelread_after(void *fixture)
{
	struct console_cmd_accelread_fixture *this = fixture;

	motion_sensors[0].drv = this->sensor_0_drv;
}

ZTEST_SUITE(console_cmd_accelread, drivers_predicate_post_main,
	    console_cmd_accelread_setup, console_cmd_accelread_before,
	    console_cmd_accelread_after, NULL);

ZTEST_USER(console_cmd_accelread, test_too_few_arguments)
{
	int rv = shell_execute_cmd(get_ec_shell(), "accelread");

	zassert_equal(EC_ERROR_PARAM_COUNT, rv, "Expected %d, but got %d",
		      EC_ERROR_PARAM_COUNT, rv);
}

ZTEST_USER(console_cmd_accelread, test_invalid_sensor_num)
{
	int rv;

	rv = shell_execute_cmd(get_ec_shell(), "accelread f");
	zassert_equal(EC_ERROR_PARAM1, rv, "Expected %d, but got %d",
		      EC_ERROR_PARAM1, rv);

	rv = shell_execute_cmd(get_ec_shell(), "accelread -1");
	zassert_equal(EC_ERROR_PARAM1, rv, "Expected %d, but got %d",
		      EC_ERROR_PARAM1, rv);

	rv = shell_execute_cmd(get_ec_shell(), "accelread 100");
	zassert_equal(EC_ERROR_PARAM1, rv, "Expected %d, but got %d",
		      EC_ERROR_PARAM1, rv);
}

static struct console_cmd_accelread_fixture *current_fixture;

int mock_read_call_super(const struct motion_sensor_t *s, int *v)
{
	return current_fixture->sensor_0_drv->read(s, v);
}

ZTEST_USER_F(console_cmd_accelread, test_read)
{
	current_fixture = this;
	mock_read_fake.custom_fake = mock_read_call_super;
	motion_sensors[0].drv = &this->mock_drv;

	zassert_ok(shell_execute_cmd(get_ec_shell(), "accelread 0"), NULL);
	zassert_equal(1, mock_read_fake.call_count,
		      "Expected only 1 call to read, but got %d",
		      mock_read_fake.call_count);

	zassert_ok(shell_execute_cmd(get_ec_shell(), "accelread 0 2"), NULL);
	zassert_equal(3, mock_read_fake.call_count,
		      "Expected only 3 call to read, but got %d",
		      mock_read_fake.call_count);
}

ZTEST_USER_F(console_cmd_accelread, test_read_fail)
{
	mock_read_fake.return_val = 1;
	motion_sensors[0].drv = &this->mock_drv;

	zassert_ok(shell_execute_cmd(get_ec_shell(), "accelread 0"), NULL);
	zassert_equal(1, mock_read_fake.call_count,
		      "Expected only 1 call to read, but got %d",
		      mock_read_fake.call_count);
}
