/* Copyright 2022 The ChromiumOS Authors.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/ztest.h>

#include "ec_commands.h"
#include "host_command.h"
#include "vstore.h"
#include "test/drivers/test_state.h"

ZTEST_SUITE(vstore, drivers_predicate_post_main, NULL, NULL, NULL, NULL);

ZTEST_USER(vstore, test_vstore_info)
{
	struct ec_response_vstore_info response;
	struct host_cmd_handler_args args =
		BUILD_HOST_COMMAND_RESPONSE(EC_CMD_VSTORE_INFO, 0, response);

	zassert_ok(host_command_process(&args), NULL);
	zassert_ok(args.result, NULL);
	zassert_equal(args.response_size, sizeof(response), NULL);
	zassert_equal(response.slot_count, CONFIG_VSTORE_SLOT_COUNT,
		      "response.slot_count = %d", response.slot_count);
	zassert_equal(response.slot_locked, 0, "response.slot_locked = %#x",
		      response.slot_locked);
}

ZTEST_USER(vstore, test_vstore_read)
{
	struct ec_params_vstore_read params = {
		.slot = 0,
	};
	struct ec_response_vstore_read response;
	struct host_cmd_handler_args args =
		BUILD_HOST_COMMAND(EC_CMD_VSTORE_READ, 0, response, params);
	uint8_t expect[EC_VSTORE_SLOT_SIZE] = {}; /* data should start as 0 */

	zassert_ok(host_command_process(&args), NULL);
	zassert_ok(args.result, NULL);
	zassert_equal(args.response_size, sizeof(response), NULL);
	zassert_mem_equal(expect, response.data, EC_VSTORE_SLOT_SIZE,
			  "response.data did not match");
}

ZTEST_USER(vstore, test_vstore_read_bad_slot)
{
	struct ec_params_vstore_read params = {
		.slot = CONFIG_VSTORE_SLOT_COUNT,
	};
	struct ec_response_vstore_read response;
	struct host_cmd_handler_args args =
		BUILD_HOST_COMMAND(EC_CMD_VSTORE_READ, 0, response, params);

	zassert_equal(host_command_process(&args), EC_RES_INVALID_PARAM,
		      "Failed to fail on invalid slot %d", params.slot);
}

ZTEST_USER(vstore, test_vstore_write_bad_slot)
{
	struct ec_params_vstore_write params = {
		.slot = CONFIG_VSTORE_SLOT_COUNT,
		.data = {},
	};
	struct host_cmd_handler_args args =
		BUILD_HOST_COMMAND_PARAMS(EC_CMD_VSTORE_WRITE, 0, params);

	zassert_equal(host_command_process(&args), EC_RES_INVALID_PARAM,
		      "Failed to fail on invalid slot %d", params.slot);
}

static void do_vstore_write_read(unsigned int slot)
{
	struct ec_params_vstore_write write_params = {
		.slot = slot,
		/* .data is set up below */
	};
	struct host_cmd_handler_args write_args =
		BUILD_HOST_COMMAND_PARAMS(EC_CMD_VSTORE_WRITE, 0, write_params);
	struct ec_params_vstore_read read_params = {
		.slot = slot,
	};
	struct ec_response_vstore_read read_response;
	struct host_cmd_handler_args read_args = BUILD_HOST_COMMAND(
		EC_CMD_VSTORE_READ, 0, read_response, read_params);
	struct ec_response_vstore_info info_response;
	struct host_cmd_handler_args info_args = BUILD_HOST_COMMAND_RESPONSE(
		EC_CMD_VSTORE_INFO, 0, info_response);
	int i;

	for (i = 0; i < EC_VSTORE_SLOT_SIZE; i++)
		write_params.data[i] = i + 1;

	/* Write to a slot */
	zassert_ok(host_command_process(&write_args), NULL);
	zassert_ok(write_args.result, NULL);

	/* Check that it is now locked */
	zassert_ok(host_command_process(&info_args), NULL);
	zassert_ok(info_args.result, NULL);
	zassert_equal(info_args.response_size, sizeof(info_response), NULL);
	zassert_equal(info_response.slot_count, CONFIG_VSTORE_SLOT_COUNT,
		      "response.slot_count = %d", info_response.slot_count);
	zassert_equal(info_response.slot_locked, 1 << slot,
		      "response.slot_locked = %#x", info_response.slot_locked);

	/* Read to check data */
	zassert_ok(host_command_process(&read_args), NULL);
	zassert_ok(read_args.result, NULL);
	zassert_equal(read_args.response_size, sizeof(read_response), NULL);
	zassert_mem_equal(write_params.data, read_response.data,
			  EC_VSTORE_SLOT_SIZE, "response.data did not match");

	/* Try to write to it again */
	zassert_equal(host_command_process(&write_args), EC_RES_ACCESS_DENIED,
		      "Failed to fail on writing locked slot %d",
		      write_params.slot);

	/* Check that it is still locked after that attempt */
	zassert_ok(host_command_process(&info_args), NULL);
	zassert_ok(info_args.result, NULL);
	zassert_equal(info_args.response_size, sizeof(info_response), NULL);
	zassert_equal(info_response.slot_count, CONFIG_VSTORE_SLOT_COUNT,
		      "response.slot_count = %d", info_response.slot_count);
	zassert_equal(info_response.slot_locked, 1 << slot,
		      "response.slot_locked = %#x", info_response.slot_locked);

	/* Read to check the data didn't change */
	zassert_ok(host_command_process(&read_args), NULL);
	zassert_ok(read_args.result, NULL);
	zassert_equal(read_args.response_size, sizeof(read_response), NULL);
	zassert_mem_equal(write_params.data, read_response.data,
			  EC_VSTORE_SLOT_SIZE, "response.data did not match");

	/* Clear locks and try the write again, this time with zero bytes  */
	vstore_clear_lock();
	memset(write_params.data, '\0', EC_VSTORE_SLOT_SIZE);
	zassert_ok(host_command_process(&write_args), NULL);
	zassert_ok(write_args.result, NULL);

	/* Check that it is now locked */
	zassert_ok(host_command_process(&info_args), NULL);
	zassert_ok(info_args.result, NULL);
	zassert_equal(info_args.response_size, sizeof(info_response), NULL);
	zassert_equal(info_response.slot_count, CONFIG_VSTORE_SLOT_COUNT,
		      "response.slot_count = %d", info_response.slot_count);
	zassert_equal(info_response.slot_locked, 1 << slot,
		      "response.slot_locked = %#x", info_response.slot_locked);

	/* Read to check the data changed */
	zassert_ok(host_command_process(&read_args), NULL);
	zassert_ok(read_args.result, NULL);
	zassert_equal(read_args.response_size, sizeof(read_response), NULL);
	zassert_mem_equal(write_params.data, read_response.data,
			  EC_VSTORE_SLOT_SIZE, "response.data did not match");

	/* Clear locks to put things into a normal state */
	vstore_clear_lock();
}

ZTEST_USER(vstore, test_vstore_write_read)
{
	/* Try on two different slots */
	zassert_true(CONFIG_VSTORE_SLOT_COUNT >= 2,
		     "Please set CONFIG_VSTORE_SLOT_COUNT to >= 2");
	do_vstore_write_read(0);
	do_vstore_write_read(1);
}
