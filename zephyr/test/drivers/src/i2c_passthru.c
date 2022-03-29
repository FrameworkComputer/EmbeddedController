/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr.h>
#include <ztest.h>

#include "ec_commands.h"
#include "host_command.h"
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

	params->port = I2C_PORT_VIRTUAL_BATTERY;
	params->num_msgs = 1;
	params->msg[0].addr_flags = VIRTUAL_BATTERY_ADDR_FLAGS |
				    EC_I2C_FLAG_READ;
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

ZTEST_SUITE(i2c_passthru, drivers_predicate_post_main, NULL, NULL, NULL, NULL);
