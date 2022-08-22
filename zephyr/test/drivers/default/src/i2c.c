/* Copyright 2022 The ChromiumOS Authors.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/zephyr.h>
#include <zephyr/ztest.h>

#include "ec_commands.h"
#include "host_command.h"
#include "i2c.h"
#include "test/drivers/test_state.h"

ZTEST_USER(i2c, test_i2c_set_speed_success)
{
	struct ec_response_i2c_control response;
	struct ec_params_i2c_control get_params = {
		.port = I2C_PORT_USB_C0,
		.cmd = EC_I2C_CONTROL_GET_SPEED,
	};
	struct host_cmd_handler_args get_args =
		BUILD_HOST_COMMAND(EC_CMD_I2C_CONTROL, 0, response, get_params);
	struct ec_params_i2c_control set_params = {
		.port = I2C_PORT_USB_C0,
		.cmd = EC_I2C_CONTROL_SET_SPEED,
	};
	struct host_cmd_handler_args set_args =
		BUILD_HOST_COMMAND(EC_CMD_I2C_CONTROL, 0, response, set_params);

	/* Get the speed: 100. */
	zassert_ok(host_command_process(&get_args), NULL);
	zassert_ok(get_args.result, NULL);
	zassert_equal(get_args.response_size, sizeof(response), NULL);
	zassert_equal(response.cmd_response.speed_khz, 100,
		      "response.cmd_response.speed_khz = %d",
		      response.cmd_response.speed_khz);

	/* Set the speed to 400. */
	set_params.cmd_params.speed_khz = 400;
	zassert_ok(host_command_process(&set_args), NULL);
	zassert_ok(set_args.result, NULL);
	zassert_equal(set_args.response_size, sizeof(response), NULL);
	zassert_equal(response.cmd_response.speed_khz, 100,
		      "response.cmd_response.speed_khz = %d",
		      response.cmd_response.speed_khz);

	/* Get the speed to verify. */
	zassert_ok(host_command_process(&get_args), NULL);
	zassert_ok(get_args.result, NULL);
	zassert_equal(get_args.response_size, sizeof(response), NULL);
	zassert_equal(response.cmd_response.speed_khz, 400,
		      "response.cmd_response.speed_khz = %d",
		      response.cmd_response.speed_khz);

	/* Set the speed back to 100. */
	set_params.cmd_params.speed_khz = 100;
	zassert_ok(host_command_process(&set_args), NULL);
	zassert_ok(set_args.result, NULL);
	zassert_equal(set_args.response_size, sizeof(response), NULL);
	zassert_equal(response.cmd_response.speed_khz, 400,
		      "response.cmd_response.speed_khz = %d",
		      response.cmd_response.speed_khz);
}

ZTEST_USER(i2c, test_i2c_set_speed_not_dynamic)
{
	struct ec_response_i2c_control response;
	struct ec_params_i2c_control set_params = {
		.port = I2C_PORT_POWER,
		.cmd = EC_I2C_CONTROL_SET_SPEED,
		.cmd_params.speed_khz = 400,
	};
	struct host_cmd_handler_args set_args =
		BUILD_HOST_COMMAND(EC_CMD_I2C_CONTROL, 0, response, set_params);

	/* Set the speed to 400 on a bus which doesn't support dynamic-speed. */
	zassert_equal(EC_RES_ERROR, host_command_process(&set_args), NULL);
}

ZTEST_USER(i2c, test_i2c_control_wrong_port)
{
	struct ec_response_i2c_control response;
	struct ec_params_i2c_control get_params = {
		.port = 10,
		.cmd = EC_I2C_CONTROL_GET_SPEED,
	};
	struct host_cmd_handler_args get_args =
		BUILD_HOST_COMMAND(EC_CMD_I2C_CONTROL, 0, response, get_params);

	/* Set the .port=10, which is not defined. */
	zassert_equal(EC_RES_INVALID_PARAM, host_command_process(&get_args),
		      NULL);
}

ZTEST_USER(i2c, test_i2c_control_wrong_cmd)
{
	struct ec_response_i2c_control response;
	struct ec_params_i2c_control params = {
		.port = I2C_PORT_USB_C0,
		.cmd = 10,
	};
	struct host_cmd_handler_args args =
		BUILD_HOST_COMMAND(EC_CMD_I2C_CONTROL, 0, response, params);

	/* Call the .cmd=10, which is not defined. */
	zassert_equal(EC_RES_INVALID_COMMAND, host_command_process(&args),
		      NULL);
}

ZTEST_USER(i2c, test_i2c_set_speed_wrong_freq)
{
	struct ec_response_i2c_control response;
	struct ec_params_i2c_control set_params = {
		.port = I2C_PORT_USB_C0,
		.cmd = EC_I2C_CONTROL_SET_SPEED,
		.cmd_params.speed_khz = 123,
	};
	struct host_cmd_handler_args set_args =
		BUILD_HOST_COMMAND(EC_CMD_I2C_CONTROL, 0, response, set_params);

	/* Set the speed to 123 KHz (an invalid speed). */
	zassert_equal(EC_RES_INVALID_PARAM, host_command_process(&set_args),
		      NULL);
}

static void i2c_freq_reset(void)
{
	/* The test modifies this port. Reset it to the DTS defined. */
	zassert_ok(i2c_set_freq(I2C_PORT_USB_C0, I2C_FREQ_100KHZ), NULL);
}

static void *i2c_setup(void)
{
	i2c_freq_reset();
	return NULL;
}

static void i2c_teardown(void *state)
{
	ARG_UNUSED(state);
	i2c_freq_reset();
}

ZTEST_SUITE(i2c, drivers_predicate_post_main, i2c_setup, NULL, NULL,
	    i2c_teardown);
