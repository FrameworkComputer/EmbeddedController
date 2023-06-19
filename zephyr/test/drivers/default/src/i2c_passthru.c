/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "driver/ln9310.h"
#include "ec_commands.h"
#include "host_command.h"
#include "i2c.h"
#include "test/drivers/test_mocks.h"
#include "test/drivers/test_state.h"
#include "test/drivers/utils.h"

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

FAKE_VALUE_FUNC(int, board_allow_i2c_passthru, const struct i2c_cmd_desc_t *);

int board_allow_i2c_passthru_custom_fake(const struct i2c_cmd_desc_t *cmd_desc)
{
	/* Only allow passthru on I2C_PORT_USB_C0 */
	return i2c_get_device_for_port(cmd_desc->port) ==
	       i2c_get_device_for_port(I2C_PORT_USB_C0);
}

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

	zassert_ok(host_command_process(&args));
	CHECK_ARGS_RESULT(args)
	zassert_equal(response->i2c_status, EC_I2C_STATUS_NAK);
	zassert_equal(args.response_size,
		      sizeof(struct ec_response_i2c_passthru), NULL);
}

ZTEST_USER(i2c_passthru, test_passthru_invalid_params)
{
	uint16_t tcpc_addr = DT_REG_ADDR(DT_NODELABEL(tcpci_emul));
	uint8_t *out_data;
	uint8_t param_buf[sizeof(struct ec_params_i2c_passthru) +
			  2 * sizeof(struct ec_params_i2c_passthru_msg) + 1];
	uint8_t response_buf[sizeof(struct ec_response_i2c_passthru) + 2];
	struct ec_params_i2c_passthru *passthru_params =
		(struct ec_params_i2c_passthru *)&param_buf;
	struct host_cmd_handler_args args =
		BUILD_HOST_COMMAND_SIMPLE(EC_CMD_I2C_PASSTHRU, 0);

	passthru_params->port = I2C_PORT_USB_C0;
	passthru_params->num_msgs = 2;
	passthru_params->msg[0].addr_flags = tcpc_addr;
	passthru_params->msg[0].len = 1;
	passthru_params->msg[1].addr_flags = tcpc_addr | EC_I2C_FLAG_READ;
	passthru_params->msg[1].len = 2; /* 2 byte vendor ID */

	/* Write data follows the passthru messages */
	out_data = (uint8_t *)&passthru_params->msg[2];
	out_data[0] = 0; /* TCPC_REG_VENDOR_ID 0x0 */

	args.params = &param_buf;
	args.params_size = sizeof(param_buf);
	args.response = &response_buf;
	args.response_max = sizeof(response_buf);

	/* Set the params_size to smaller than struct ec_params_i2c_passthru */
	args.params_size = 1;
	zassert_equal(host_command_process(&args), EC_RES_INVALID_PARAM);

	/* Set the params_size so it truncates the 2nd I2C message */
	args.params_size = sizeof(struct ec_params_i2c_passthru) +
			   sizeof(struct ec_params_i2c_passthru_msg);
	zassert_equal(host_command_process(&args), EC_RES_INVALID_PARAM);

	/* Don't provide enough room for the response */
	args.params_size = sizeof(param_buf);
	args.response_max = sizeof(struct ec_response_i2c_passthru) + 1;
	zassert_equal(host_command_process(&args), EC_RES_INVALID_PARAM);

	/* Don't provide the write data */
	args.response_max = sizeof(response_buf);
	args.params_size = sizeof(struct ec_params_i2c_passthru) +
			   2 * sizeof(struct ec_params_i2c_passthru_msg);
	zassert_equal(host_command_process(&args), EC_RES_INVALID_PARAM);
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

	/* Check the protect status: 0 (unprotected) */
	zassert_ok(ec_cmd_i2c_passthru_protect(&status_args, &status_params,
					       &response),
		   NULL);
	zassert_equal(status_args.response_size, sizeof(response), NULL);
	zassert_equal(response.status, 0, "response.status = %d",
		      response.status);

	/* Protect the bus */
	zassert_ok(ec_cmd_i2c_passthru_protect(NULL, &enable_params, &response),
		   NULL);

	/* Check the protect status: 1 (protected) */
	zassert_ok(ec_cmd_i2c_passthru_protect(&status_args, &status_params,
					       &response),
		   NULL);
	zassert_equal(status_args.response_size, sizeof(response), NULL);
	zassert_equal(response.status, 1, "response.status = %d",
		      response.status);

	/* Error case: wrong subcmd */
	status_params.subcmd = 10;
	zassert_equal(ec_cmd_i2c_passthru_protect(NULL, &status_params,
						  &response),
		      EC_RES_INVALID_COMMAND, NULL);
	status_params.subcmd = EC_CMD_I2C_PASSTHRU_PROTECT_STATUS;

	/* Error case: wrong port */
	status_params.port = 10;
	zassert_equal(ec_cmd_i2c_passthru_protect(NULL, &status_params,
						  &response),
		      EC_RES_INVALID_PARAM, NULL);
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
	struct ec_response_i2c_passthru_protect enable_response;
	uint16_t tcpc_addr = DT_REG_ADDR(DT_NODELABEL(tcpci_emul));
	uint8_t *out_data;
	uint8_t param_buf[sizeof(struct ec_params_i2c_passthru) +
			  2 * sizeof(struct ec_params_i2c_passthru_msg) + 1];
	uint8_t response_buf[sizeof(struct ec_response_i2c_passthru) + 2];
	struct ec_params_i2c_passthru *passthru_params =
		(struct ec_params_i2c_passthru *)&param_buf;
	struct ec_response_i2c_passthru *passthru_response =
		(struct ec_response_i2c_passthru *)&response_buf;
	struct host_cmd_handler_args passthru_args =
		BUILD_HOST_COMMAND_SIMPLE(EC_CMD_I2C_PASSTHRU, 0);

	/* If the system is unlocked, TCPC protection is disabled */
	system_is_locked_fake.return_val = false;

	/* Protect the all TCPC buses */
	zassert_ok(ec_cmd_i2c_passthru_protect(NULL, &enable_params,
					       &enable_response),
		   NULL);

	passthru_params->port = I2C_PORT_USB_C0;
	passthru_params->num_msgs = 2;
	passthru_params->msg[0].addr_flags = tcpc_addr;
	passthru_params->msg[0].len = 1;
	passthru_params->msg[1].addr_flags = tcpc_addr | EC_I2C_FLAG_READ;
	passthru_params->msg[1].len = 2; /* 2 byte vendor ID */

	/* Write data follows the passthru messages */
	out_data = (uint8_t *)&passthru_params->msg[2];
	out_data[0] = 0; /* TCPC_REG_VENDOR_ID 0x0 */

	passthru_args.params = &param_buf;
	passthru_args.params_size = sizeof(param_buf);
	passthru_args.response = &response_buf;
	passthru_args.response_max = sizeof(response_buf);

	zassert_ok(host_command_process(&passthru_args));
	CHECK_ARGS_RESULT(args)
	zassert_ok(passthru_response->i2c_status);
	zassert_equal(passthru_args.response_size,
		      sizeof(struct ec_response_i2c_passthru) + 2, NULL);

	/* Now attempt TCPC protection while the system is locked */
	system_is_locked_fake.return_val = true;

	/* Protect the all TCPC buses */
	zassert_ok(ec_cmd_i2c_passthru_protect(NULL, &enable_params,
					       &enable_response),
		   NULL);

	zassert_equal(host_command_process(&passthru_args),
		      EC_RES_ACCESS_DENIED);
}

ZTEST_USER(i2c_passthru, test_passthru_restricted)
{
	uint16_t tcpc_addr = DT_REG_ADDR(DT_NODELABEL(tcpci_emul));
	uint16_t ps8xxx_addr = DT_REG_ADDR(DT_NODELABEL(ps8xxx_emul));
	uint8_t *out_data;
	uint8_t tcpc_param_buf[sizeof(struct ec_params_i2c_passthru) +
			       2 * sizeof(struct ec_params_i2c_passthru_msg) +
			       1];
	uint8_t tcpc_rsp_buf[sizeof(struct ec_response_i2c_passthru) + 2];
	struct ec_params_i2c_passthru *tcpc_params =
		(struct ec_params_i2c_passthru *)&tcpc_param_buf;
	struct ec_response_i2c_passthru *tcpc_response =
		(struct ec_response_i2c_passthru *)&tcpc_rsp_buf;
	struct host_cmd_handler_args tcpc_args =
		BUILD_HOST_COMMAND_SIMPLE(EC_CMD_I2C_PASSTHRU, 0);

	uint8_t ps8xxx_param_buf[sizeof(struct ec_params_i2c_passthru) +
				 2 * sizeof(struct ec_params_i2c_passthru_msg) +
				 1];
	uint8_t ps8xxx_rsp_buf[sizeof(struct ec_response_i2c_passthru) + 2];
	struct ec_params_i2c_passthru *ps8xxx_params =
		(struct ec_params_i2c_passthru *)&ps8xxx_param_buf;
	struct ec_response_i2c_passthru *ps8xxx_response =
		(struct ec_response_i2c_passthru *)&ps8xxx_rsp_buf;
	struct host_cmd_handler_args ps8xxx_args =
		BUILD_HOST_COMMAND_SIMPLE(EC_CMD_I2C_PASSTHRU, 0);

	if (!IS_ENABLED(CONFIG_PLATFORM_EC_I2C_PASSTHRU_RESTRICTED)) {
		ztest_test_skip();
		return;
	}

	/*
	 * Setup passthru command to the TCPCI emulator - which is always
	 * permitted by our board_allow_i2c_passthru() fake.
	 */
	tcpc_params->port = I2C_PORT_USB_C0;
	tcpc_params->num_msgs = 2;
	tcpc_params->msg[0].addr_flags = tcpc_addr;
	tcpc_params->msg[0].len = 1;
	tcpc_params->msg[1].addr_flags = tcpc_addr | EC_I2C_FLAG_READ;
	tcpc_params->msg[1].len = 2; /* 2 byte vendor ID */

	/* Write data follows the passthru messages */
	out_data = (uint8_t *)&tcpc_params->msg[2];
	out_data[0] = 0; /* TCPC_REG_VENDOR_ID 0x0 */

	tcpc_args.params = &tcpc_param_buf;
	tcpc_args.params_size = sizeof(tcpc_param_buf);
	tcpc_args.response = &tcpc_rsp_buf;
	tcpc_args.response_max = sizeof(tcpc_rsp_buf);

	/*
	 * Setup passthru command to the PS8xxx emulator, which should be
	 * rejected when the system is locked.
	 */
	ps8xxx_params->port = I2C_PORT_USB_C1;
	ps8xxx_params->num_msgs = 2;
	ps8xxx_params->msg[0].addr_flags = ps8xxx_addr;
	ps8xxx_params->msg[0].len = 1;
	ps8xxx_params->msg[1].addr_flags = ps8xxx_addr | EC_I2C_FLAG_READ;
	ps8xxx_params->msg[1].len = 2; /* 2-byte vendor ID */

	/* Write data follows the passthru messages */
	out_data = (uint8_t *)&ps8xxx_params->msg[2];
	out_data[0] = 0; /* TCPC_REG_VENDOR_ID 0x0 */

	ps8xxx_args.params = &ps8xxx_param_buf;
	ps8xxx_args.params_size = sizeof(ps8xxx_param_buf);
	ps8xxx_args.response = &ps8xxx_rsp_buf;
	ps8xxx_args.response_max = sizeof(ps8xxx_rsp_buf);

	/* Install our board_allow_i2c_passthru() handler */
	board_allow_i2c_passthru_fake.custom_fake =
		board_allow_i2c_passthru_custom_fake;

	/* When the system is unlocked, no restrictions apply */
	system_is_locked_fake.return_val = false;

	zassert_ok(host_command_process(&tcpc_args));
	CHECK_ARGS_RESULT(tcpc_args)
	zassert_ok(tcpc_response->i2c_status);
	zassert_equal(tcpc_args.response_size,
		      sizeof(struct ec_response_i2c_passthru) + 2, NULL);

	zassert_ok(host_command_process(&ps8xxx_args));
	CHECK_ARGS_RESULT(ps8xxx_args)
	zassert_ok(ps8xxx_response->i2c_status);
	zassert_equal(ps8xxx_args.response_size,
		      sizeof(struct ec_response_i2c_passthru) + 2, NULL);

	/* Lock the system which enables board_allow_i2c_passthru() */
	system_is_locked_fake.return_val = true;

	zassert_ok(host_command_process(&tcpc_args));
	CHECK_ARGS_RESULT(tcpc_args)
	zassert_ok(tcpc_response->i2c_status);
	zassert_equal(tcpc_args.response_size,
		      sizeof(struct ec_response_i2c_passthru) + 2, NULL);

	zassert_equal(host_command_process(&ps8xxx_args), EC_RES_ACCESS_DENIED);
}

static void i2c_passthru_before(void *state)
{
	ARG_UNUSED(state);
	RESET_FAKE(board_allow_i2c_passthru);
	board_allow_i2c_passthru_fake.return_val = 1;
}

static void i2c_passthru_after(void *state)
{
	ARG_UNUSED(state);
	i2c_passthru_protect_reset();
}

ZTEST_SUITE(i2c_passthru, drivers_predicate_post_main, NULL,
	    i2c_passthru_before, i2c_passthru_after, NULL);
