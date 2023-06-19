/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/**
 * @file
 * @brief Unit Tests for ESPI port 80 writes
 */

#include "console.h"
#include "ec_commands.h"
#include "host_command.h"
#include "port80.h"
#include "test/drivers/test_state.h"
#include "test/drivers/utils.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/shell/shell.h>
#include <zephyr/ztest.h>

/*
 * Flush any existing writes.
 */
static void port80_flush(void)
{
	zassert_ok(shell_execute_cmd(get_ec_shell(), "port80 flush"), NULL);
}

/**
 * @brief TestPurpose: Verify port 80 writes
 *
 * @details
 * Validate that the port 80 writes are processed correctly.
 *
 * Expected Results
 *  - The port 80 writes are received
 */
ZTEST(port80, test_port80_write)
{
	struct ec_response_port80_read response;
	struct ec_params_port80_read params;
	struct host_cmd_handler_args args =
		BUILD_HOST_COMMAND(EC_CMD_PORT80_READ, 1, response, params);

	port80_flush();
	port_80_write(0x12);
	port_80_write(0x34);
	/* Check the buffer using the host cmd */

	/* Get the buffer info */
	params.subcmd = EC_PORT80_GET_INFO;
	zassert_ok(host_command_process(&args), NULL);
	CHECK_ARGS_RESULT(args)
	zassert_equal(args.response_size, sizeof(response.get_info), NULL);
	zassert_equal(response.get_info.writes, 2, NULL);
	/* Read the buffer */
	params.subcmd = EC_PORT80_READ_BUFFER;
	params.read_buffer.offset = 0;
	params.read_buffer.num_entries = 2;
	zassert_ok(host_command_process(&args), NULL);
	CHECK_ARGS_RESULT(args)
	zassert_equal(args.response_size, sizeof(uint16_t) * 2, NULL);
	zassert_equal(response.data.codes[0], 0x12, NULL);
	zassert_equal(response.data.codes[1], 0x34, NULL);
}

/**
 * @brief TestPurpose: Verify port 80 read parameters
 *
 * @details
 * Validate that the port 80 read parameters are checked
 *
 * Expected Results
 *  - The port 80 parameters are verified
 */
ZTEST(port80, test_port80_offset)
{
	struct ec_response_port80_read response;
	struct ec_params_port80_read params;
	struct host_cmd_handler_args args =
		BUILD_HOST_COMMAND(EC_CMD_PORT80_READ, 1, response, params);

	port80_flush();

	params.subcmd = EC_PORT80_READ_BUFFER;
	params.read_buffer.offset = 0;
	params.read_buffer.num_entries = 0;
	zassert_equal(host_command_process(&args), EC_RES_INVALID_PARAM, NULL);
	params.read_buffer.offset = 0xFFFF;
	params.read_buffer.num_entries = 2;
	zassert_equal(host_command_process(&args), EC_RES_INVALID_PARAM, NULL);
	params.read_buffer.offset = 0;
	params.read_buffer.num_entries = 0xFFFF;
	zassert_equal(host_command_process(&args), EC_RES_INVALID_PARAM, NULL);
}

/**
 * @brief TestPurpose: Verify port 80 reset event
 *
 * @details
 * Validate that the port 80 handling works for the reset event
 *
 * Expected Results
 *  - The port 80 handling detects the reset event.
 */
ZTEST(port80, test_port80_special)
{
	struct ec_params_port80_read params;
	struct ec_response_port80_last_boot response;
	struct host_cmd_handler_args args =
		BUILD_HOST_COMMAND(EC_CMD_PORT80_READ, 0, response, params);

	port80_flush();
	port_80_write(0xDEAD);
	port_80_write(0xAA); /* must be < 0x100 */
	port_80_write(PORT_80_EVENT_RESET);
	/* Check the buffer using the host cmd version 0*/
	zassert_ok(host_command_process(&args), NULL);
	CHECK_ARGS_RESULT(args)
	zassert_equal(args.response_size, sizeof(response), NULL);
	zassert_equal(response.code, 0xAA, NULL);
}

/**
 * @brief TestPurpose: Verify port 80 subcommand
 *
 * @details
 * Validate that the port 80 host subcommand is checked.
 *
 * Expected Results
 *  - The port 80 handling detects an invalid subcommand.
 */
ZTEST(port80, test_port80_subcmd)
{
	struct ec_params_port80_read params;
	struct ec_response_port80_last_boot response;
	struct host_cmd_handler_args args =
		BUILD_HOST_COMMAND(EC_CMD_PORT80_READ, 1, response, params);

	params.subcmd = 0xFFFF;
	zassert_ok(!host_command_process(&args), NULL);
}

/**
 * @brief TestPurpose: Verify port 80 write wrap
 *
 * @details
 * Validate that the port 80 host writes wrap around.
 *
 * Expected Results
 *  - The port 80 writes overwrites the history array.
 */
ZTEST(port80, test_port80_wrap)
{
	struct ec_params_port80_read params;
	struct ec_response_port80_read response;
	struct host_cmd_handler_args args =
		BUILD_HOST_COMMAND(EC_CMD_PORT80_READ, 1, response, params);
	uint32_t size, count;

	port80_flush();
	/* Get the history array size */
	params.subcmd = EC_PORT80_GET_INFO;
	zassert_ok(host_command_process(&args), NULL);
	CHECK_ARGS_RESULT(args)
	zassert_equal(args.response_size, sizeof(response.get_info), NULL);
	size = response.get_info.history_size;
	count = size + size / 2; /* Ensure write will wrap the history */
	for (uint32_t i = 0; i < count; i++) {
		port_80_write(i);
	}
	/*
	 * Retrieve the first entry in the history array.
	 * It should equal the size of the array.
	 */
	params.subcmd = EC_PORT80_READ_BUFFER;
	params.read_buffer.offset = 0;
	params.read_buffer.num_entries = 1;
	zassert_ok(host_command_process(&args), NULL);
	CHECK_ARGS_RESULT(args)
	zassert_equal(args.response_size, sizeof(uint16_t), NULL);
	zassert_equal(response.data.codes[0], size, NULL);
}

/**
 * @brief Test Suite: Verifies port 80 writes.
 */
ZTEST_SUITE(port80, drivers_predicate_post_main, NULL, NULL, NULL, NULL);
