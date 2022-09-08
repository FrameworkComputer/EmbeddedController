/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#include "driver/ln9310.h"
#include "ec_commands.h"
#include "host_command.h"
#include "i2c.h"
#include "test/drivers/test_state.h"

ZTEST_USER(i2c_passthru, test_read_without_write)
{
	uint8_t param_buf[sizeof(struct ec_params_i2c_passthru) +
			  sizeof(struct ec_params_i2c_passthru_msg)];
	uint8_t response_buf[sizeof(struct ec_response_i2c_passthru) + 2];
	struct ec_params_i2c_passthru *params =
		(struct ec_params_i2c_passthru *)&param_buf;
	struct ec_response_i2c_passthru *response =
		(struct ec_response_i2c_passthru *)&response_buf;
	struct host_cmd_handler_args args =
		BUILD_HOST_COMMAND_SIMPLE(EC_CMD_I2C_PASSTHRU, 0);

	params->port = I2C_PORT_POWER;
	params->num_msgs = 1;
	params->msg[0].addr_flags = LN9310_I2C_ADDR_0_FLAGS | EC_I2C_FLAG_READ;
	params->msg[0].len = 1;
	args.params = &param_buf;
	args.params_size = sizeof(param_buf);
	args.response = &response_buf;
	args.response_max = sizeof(response_buf);

	zassert_ok(host_command_process(&args), NULL);
	zassert_ok(args.result, NULL);
	zassert_equal(response->i2c_status, EC_I2C_STATUS_NAK, NULL);
	zassert_equal(args.response_size,
		      sizeof(struct ec_response_i2c_passthru), NULL);
}

ZTEST_USER(i2c_passthru, test_passthru_protect)
{
	struct ec_response_i2c_passthru_protect response;
	struct ec_params_i2c_passthru_protect status_params = {
		.port = I2C_PORT_SENSOR,
		.subcmd = EC_CMD_I2C_PASSTHRU_PROTECT_STATUS,
	};
	struct host_cmd_handler_args status_args = BUILD_HOST_COMMAND(
		EC_CMD_I2C_PASSTHRU_PROTECT, 0, response, status_params);
	struct ec_params_i2c_passthru_protect enable_params = {
		.port = I2C_PORT_SENSOR,
		.subcmd = EC_CMD_I2C_PASSTHRU_PROTECT_ENABLE,
	};
	struct host_cmd_handler_args enable_args = BUILD_HOST_COMMAND_PARAMS(
		EC_CMD_I2C_PASSTHRU_PROTECT, 0, enable_params);

	/* Check the protect status: 0 (unprotected) */
	zassert_ok(host_command_process(&status_args), NULL);
	zassert_ok(status_args.result, NULL);
	zassert_equal(status_args.response_size, sizeof(response), NULL);
	zassert_equal(response.status, 0, "response.status = %d",
		      response.status);

	/* Protect the bus */
	zassert_ok(host_command_process(&enable_args), NULL);
	zassert_ok(enable_args.result, NULL);

	/* Check the protect status: 1 (protected) */
	zassert_ok(host_command_process(&status_args), NULL);
	zassert_ok(status_args.result, NULL);
	zassert_equal(status_args.response_size, sizeof(response), NULL);
	zassert_equal(response.status, 1, "response.status = %d",
		      response.status);

	/* Error case: wrong subcmd */
	status_params.subcmd = 10;
	zassert_equal(host_command_process(&status_args),
		      EC_RES_INVALID_COMMAND, NULL);
	status_params.subcmd = EC_CMD_I2C_PASSTHRU_PROTECT_STATUS;

	/* Error case: wrong port */
	status_params.port = 10;
	zassert_equal(host_command_process(&status_args), EC_RES_INVALID_PARAM,
		      NULL);
	status_params.port = I2C_PORT_SENSOR;

	/* Error case: response size not enough */
	status_args.response_max = 0;
	zassert_equal(host_command_process(&status_args), EC_RES_INVALID_PARAM,
		      NULL);
	status_args.response_max = sizeof(response);

	/* Error case: params size not enough */
	status_args.params_size = 0;
	zassert_equal(host_command_process(&status_args), EC_RES_INVALID_PARAM,
		      NULL);
	status_args.params_size = sizeof(status_params);
}

ZTEST_USER(i2c_passthru, test_passthru_protect_tcpcs)
{
	struct ec_params_i2c_passthru_protect enable_params = {
		.port = I2C_PORT_SENSOR,
		.subcmd = EC_CMD_I2C_PASSTHRU_PROTECT_ENABLE_TCPCS,
	};
	struct host_cmd_handler_args enable_args = BUILD_HOST_COMMAND_PARAMS(
		EC_CMD_I2C_PASSTHRU_PROTECT, 0, enable_params);

	/* Protect the all TCPC buses */
	zassert_ok(host_command_process(&enable_args), NULL);
	zassert_ok(enable_args.result, NULL);
}

static void i2c_passthru_after(void *state)
{
	ARG_UNUSED(state);
	i2c_passthru_protect_reset();
}

ZTEST_SUITE(i2c_passthru, drivers_predicate_post_main, NULL, NULL,
	    i2c_passthru_after, NULL);
