/* Copyright 2022 The ChromiumOS Authors.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/zephyr.h>
#include <zephyr/ztest.h>

#include "ec_commands.h"
#include "host_command.h"
#include "test/drivers/test_state.h"

ZTEST_USER(system, test_hostcmd_sysinfo)
{
	struct ec_response_sysinfo response;
	struct host_cmd_handler_args args =
		BUILD_HOST_COMMAND_RESPONSE(EC_CMD_SYSINFO, 0, response);

	/* Simply issue the command and get the results */
	zassert_ok(host_command_process(&args), NULL);
	zassert_ok(args.result, NULL);
	zassert_equal(args.response_size, sizeof(response), NULL);
	zassert_equal(response.reset_flags, 0, "response.reset_flags = %d",
		      response.reset_flags);
	zassert_equal(response.current_image, EC_IMAGE_RO,
		      "response.current_image = %d", response.current_image);
	zassert_equal(response.flags, 0, "response.flags = %d", response.flags);
}

ZTEST_USER(system, test_hostcmd_board_version)
{
	struct ec_response_board_version response;
	struct host_cmd_handler_args args = BUILD_HOST_COMMAND_RESPONSE(
		EC_CMD_GET_BOARD_VERSION, 0, response);

	/* Get the board version, which is default 0. */
	zassert_ok(host_command_process(&args), NULL);
	zassert_ok(args.result, NULL);
	zassert_equal(args.response_size, sizeof(response), NULL);
	zassert_equal(response.board_version, 0, "response.board_version = %d",
		      response.board_version);
}

ZTEST_SUITE(system, drivers_predicate_post_main, NULL, NULL, NULL, NULL);
