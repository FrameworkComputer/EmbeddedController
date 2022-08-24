/* Copyright 2022 The ChromiumOS Authors.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/**
 * @file
 * @brief Unit Tests for ESPI port 80 writes
 */

#include <zephyr/logging/log.h>
#include <zephyr/shell/shell.h>
#include <zephyr/zephyr.h>
#include <zephyr/ztest.h>

#include "console.h"
#include "ec_commands.h"
#include "host_command.h"
#include "port80.h"

#include "test/drivers/test_state.h"
#include "test/drivers/utils.h"

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
		BUILD_HOST_COMMAND_SIMPLE(EC_CMD_PORT80_READ, 1);

	port80_flush();
	port_80_write(0x12);
	port_80_write(0x34);
	/* Check the buffer using the host cmd */

	args.params = &params;
	args.params_size = sizeof(params);
	args.response = &response;
	args.response_max = sizeof(response);
	/* Get the buffer info */
	params.subcmd = EC_PORT80_GET_INFO;
	zassert_ok(host_command_process(&args), NULL);
	zassert_ok(args.result, NULL);
	zassert_equal(args.response_size, sizeof(response.get_info), NULL);
	zassert_equal(response.get_info.writes, 2, NULL);
	/* Read the buffer */
	params.subcmd = EC_PORT80_READ_BUFFER;
	params.read_buffer.offset = 0;
	params.read_buffer.num_entries = 2;
	zassert_ok(host_command_process(&args), NULL);
	zassert_ok(args.result, NULL);
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
		BUILD_HOST_COMMAND_SIMPLE(EC_CMD_PORT80_READ, 1);

	port80_flush();

	args.params = &params;
	args.params_size = sizeof(params);
	args.response = &response;
	args.response_max = sizeof(response);
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
 * @brief Test Suite: Verifies port 80 writes.
 */
ZTEST_SUITE(port80, drivers_predicate_post_main, NULL, NULL, NULL, NULL);
