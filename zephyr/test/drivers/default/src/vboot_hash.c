/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>
#include <sha256.h>

#include "ec_commands.h"
#include "host_command.h"
#include "test/drivers/test_state.h"

ZTEST_USER(vboot_hash, test_hostcmd_abort)
{
	struct ec_response_vboot_hash response;
	struct ec_params_vboot_hash start_params = {
		.cmd = EC_VBOOT_HASH_START,
		.hash_type = EC_VBOOT_HASH_TYPE_SHA256,
		.offset = EC_VBOOT_HASH_OFFSET_RO,
		.size = 0,
	};
	struct host_cmd_handler_args start_args = BUILD_HOST_COMMAND(
		EC_CMD_VBOOT_HASH, 0, response, start_params);
	struct ec_params_vboot_hash abort_params = {
		.cmd = EC_VBOOT_HASH_ABORT,
	};
	struct host_cmd_handler_args abort_args =
		BUILD_HOST_COMMAND_PARAMS(EC_CMD_VBOOT_HASH, 0, abort_params);
	struct ec_params_vboot_hash get_params = {
		.cmd = EC_VBOOT_HASH_GET,
	};
	struct host_cmd_handler_args get_args =
		BUILD_HOST_COMMAND(EC_CMD_VBOOT_HASH, 0, response, get_params);

	/* Start hashing. The command doesn't wait to finish. */
	zassert_ok(host_command_process(&start_args), NULL);
	zassert_ok(start_args.result, NULL);
	zassert_equal(start_args.response_size, sizeof(response), NULL);
	zassert_equal(response.status, EC_VBOOT_HASH_STATUS_BUSY,
		      "response.status = %d", response.status);

	/* Abort it immediately */
	zassert_ok(host_command_process(&abort_args), NULL);
	zassert_ok(abort_args.result, NULL);

	/* Give it a bit time. The abort is being processed in the background */
	k_msleep(20);

	/* Get the hash result. Should be NONE. */
	zassert_ok(host_command_process(&get_args), NULL);
	zassert_ok(get_args.result, NULL);
	zassert_equal(get_args.response_size, sizeof(response), NULL);
	zassert_equal(response.status, EC_VBOOT_HASH_STATUS_NONE,
		      "response.status = %d", response.status);
}

ZTEST_USER(vboot_hash, test_hostcmd_recalc)
{
	struct ec_response_vboot_hash response;
	struct ec_params_vboot_hash recalc_params = {
		.cmd = EC_VBOOT_HASH_RECALC,
		.hash_type = EC_VBOOT_HASH_TYPE_SHA256,
		.offset = EC_VBOOT_HASH_OFFSET_RO,
		.size = 0,
	};
	struct host_cmd_handler_args recalc_args = BUILD_HOST_COMMAND(
		EC_CMD_VBOOT_HASH, 0, response, recalc_params);

	/* Recalculate the hash. The command waits to finish. */
	zassert_ok(host_command_process(&recalc_args), NULL);
	zassert_ok(recalc_args.result, NULL);
	zassert_equal(recalc_args.response_size, sizeof(response), NULL);
	zassert_equal(response.status, EC_VBOOT_HASH_STATUS_DONE,
		      "response.status = %d", response.status);
	zassert_equal(response.digest_size, SHA256_DIGEST_SIZE,
		      "response.digest_size = %d", response.digest_size);
}

ZTEST_USER(vboot_hash, test_hostcmd_hash_arbitrary_size)
{
	struct ec_response_vboot_hash response;
	struct ec_params_vboot_hash recalc_params = {
		.cmd = EC_VBOOT_HASH_RECALC,
		.hash_type = EC_VBOOT_HASH_TYPE_SHA256,
		.offset = 0,
		/* arbitrary size */
		.size = 0x12345,
	};
	struct host_cmd_handler_args recalc_args = BUILD_HOST_COMMAND(
		EC_CMD_VBOOT_HASH, 0, response, recalc_params);

	/* Recalculate the hash. The command waits to finish. */
	zassert_ok(host_command_process(&recalc_args), NULL);
	zassert_ok(recalc_args.result, NULL);
	zassert_equal(recalc_args.response_size, sizeof(response), NULL);
	zassert_equal(response.status, EC_VBOOT_HASH_STATUS_DONE,
		      "response.status = %d", response.status);
	zassert_equal(response.digest_size, SHA256_DIGEST_SIZE,
		      "response.digest_size = %d", response.digest_size);
}

ZTEST_SUITE(vboot_hash, drivers_predicate_post_main, NULL, NULL, NULL, NULL);
