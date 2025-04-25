/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "ec_commands.h"
#include "host_command.h"
#include "test/drivers/test_state.h"

#include <zephyr/fff.h>
#include <zephyr/ztest.h>

ZTEST(ec_cmd_read_memmap, test_id)
{
	struct ec_params_read_memmap params = {
		.offset = EC_MEMMAP_ID,
		.size = 2,
	};
	uint8_t response[2];
	int rv;
	struct host_cmd_handler_args args =
		BUILD_HOST_COMMAND(EC_CMD_READ_MEMMAP, 0, response, params);

	rv = host_command_process(&args);

	if (IS_ENABLED(CONFIG_HOSTCMD_X86)) {
		zassert_equal(rv, EC_RES_INVALID_COMMAND, "Got %d", rv);
		return;
	}

	zassert_ok(rv, "Got %d", rv);
	zassert_equal(args.response_size, sizeof(response));
	/* Response should be 'E' 'C' */
	zassert_equal('E', response[0]);
	zassert_equal('C', response[1]);
}

ZTEST(ec_cmd_read_memmap, test_switches)
{
	struct ec_params_read_memmap params = {
		.offset = EC_MEMMAP_SWITCHES,
		.size = 1,
	};
	uint8_t response[1];
	int rv;
	struct host_cmd_handler_args args =
		BUILD_HOST_COMMAND(EC_CMD_READ_MEMMAP, 0, response, params);

	rv = host_command_process(&args);

	if (IS_ENABLED(CONFIG_HOSTCMD_X86)) {
		zassert_equal(rv, EC_RES_INVALID_COMMAND, "Got %d", rv);
		return;
	}

	/* This test suite is run with CONFIG_PLATFORM_EC_SWITCH enabled
	 * and disabled.
	 */
	if (IS_ENABLED(CONFIG_PLATFORM_EC_SWITCH)) {
		zassert_ok(rv, "Got %d", rv);
		zassert_equal(args.response_size, sizeof(response));
	} else {
		zassert_equal(rv, EC_RES_UNAVAILABLE, "Got %d", rv);
	}
}

ZTEST(ec_cmd_read_memmap, test_invalid)
{
	struct ec_params_read_memmap params = {
		.offset = EC_MEMMAP_ID,
		.size = UINT8_MAX,
	};
	uint8_t response[2];
	int rv;
	struct host_cmd_handler_args args =
		BUILD_HOST_COMMAND(EC_CMD_READ_MEMMAP, 0, response, params);

	rv = host_command_process(&args);

	if (IS_ENABLED(CONFIG_HOSTCMD_X86)) {
		zassert_equal(rv, EC_RES_INVALID_COMMAND, "Got %d", rv);
		return;
	}

	/* Verify offset+size exceeding max fails */
	zassert_equal(rv, EC_RES_INVALID_PARAM, "Got %d", rv);

	/* Verify params.size > response_max fails */
	params.size = 4;
	zassert_equal(host_command_process(&args), EC_RES_INVALID_PARAM);
}

ZTEST_SUITE(ec_cmd_read_memmap, drivers_predicate_post_main, NULL, NULL, NULL,
	    NULL);
