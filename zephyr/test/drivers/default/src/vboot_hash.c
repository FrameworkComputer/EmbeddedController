/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/zephyr.h>
#include <ztest.h>
#include <sha256.h>

#include "ec_commands.h"
#include "host_command.h"
#include "test/drivers/test_state.h"

ZTEST_USER(vboot_hash, test_hostcmd)
{
	struct ec_params_vboot_hash params = {
		.cmd = EC_VBOOT_HASH_START,
		.offset = 0,
		.size = 0,
	};
	struct ec_response_vboot_hash response;
	struct host_cmd_handler_args args =
		BUILD_HOST_COMMAND(EC_CMD_VBOOT_HASH, 0, response, params);

	zassert_ok(host_command_process(&args), NULL);
	zassert_ok(args.result, NULL);
	zassert_equal(args.response_size, sizeof(response), NULL);
	zassert_equal(response.status, EC_VBOOT_HASH_STATUS_BUSY,
		      "response.status = %d", response.status);
}

ZTEST_SUITE(vboot_hash, drivers_predicate_post_main, NULL, NULL, NULL, NULL);
