/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "ec_commands.h"
#include "host_command.h"

#include <zephyr/ztest.h>

ZTEST_USER(system, test_hostcmd_board_version)
{
	struct ec_response_board_version response;
	struct host_cmd_handler_args args;

	/* Get the board version, which is default 0. */
	zassert_ok(ec_cmd_get_board_version(&args, &response), NULL);
	zassert_equal(args.response_size, sizeof(response), NULL);
	zassert_equal(response.board_version, 0, "response.board_version = %d",
		      response.board_version);
}
