/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "console.h"
#include "ec_commands.h"
#include "host_command.h"
#include "test/drivers/test_mocks.h"
#include "test/drivers/test_state.h"
#include "trng.h"

#include <zephyr/shell/shell_dummy.h>
#include <zephyr/ztest.h>

ZTEST_USER(random, test_hostcmd_rand)
{
	uint8_t rand_response1[16];
	uint8_t rand_response2[16];
	struct ec_params_rand_num params = {
		.num_rand_bytes = 16,
	};
	struct host_cmd_handler_args args1 = BUILD_HOST_COMMAND(
		EC_CMD_RAND_NUM, EC_VER_RAND_NUM, rand_response1, params);
	struct host_cmd_handler_args args2 = BUILD_HOST_COMMAND(
		EC_CMD_RAND_NUM, EC_VER_RAND_NUM, rand_response2, params);

	/*
	 * It's necessary to set response_max, because 'ec_response_rand_num'
	 * structure has flexible array member.
	 */
	args1.response_max = 16;
	args2.response_max = 16;
	system_is_locked_fake.return_val = 0;

	zassert_ok(host_command_process(&args1), NULL);
	zassert_ok(host_command_process(&args2), NULL);
	zassert_equal(args1.response_size, params.num_rand_bytes);
	zassert_equal(args2.response_size, params.num_rand_bytes);
	zassert_true(memcmp(rand_response1, rand_response2,
			    params.num_rand_bytes) != 0);
	zassert_equal(system_is_locked_fake.call_count, 2);
}

ZTEST_USER(random, test_hostcmd_rand_overflow)
{
	uint8_t rand_response[16];
	struct ec_params_rand_num params = {
		.num_rand_bytes = 16,
	};
	struct host_cmd_handler_args args = BUILD_HOST_COMMAND(
		EC_CMD_RAND_NUM, EC_VER_RAND_NUM, rand_response, params);

	/* Change maximum response size to small value. */
	args.response_max = 8;
	system_is_locked_fake.return_val = 0;

	zassert_equal(host_command_process(&args), EC_RES_OVERFLOW);
}

ZTEST_USER(random, test_hostcmd_rand_access_denied)
{
	uint8_t rand_response[16];
	struct ec_params_rand_num params = {
		.num_rand_bytes = 16,
	};

	system_is_locked_fake.return_val = 1;

	zassert_equal(
		ec_cmd_rand_num(NULL, &params,
				(struct ec_response_rand_num *)rand_response),
		EC_RES_ACCESS_DENIED, NULL);
	zassert_equal(system_is_locked_fake.call_count, 1);
}

ZTEST_USER(random, test_console_cmd_rand)
{
	const struct shell *shell_zephyr = get_ec_shell();
	const char *outbuffer;
	size_t buffer_size;

	shell_backend_dummy_clear_output(shell_zephyr);

	zassert_ok(shell_execute_cmd(shell_zephyr, "rand"));
	outbuffer = shell_backend_dummy_get_output(shell_zephyr, &buffer_size);

	/*
	 * Output has "rand 64_random_characters" format, so buffer should have
	 * at least 69 characters.
	 */
	zassert_true(buffer_size >= 69, "buffer size is %d", buffer_size);
	zassert_not_null(strstr(outbuffer, "rand "));
}

ZTEST_SUITE(random, drivers_predicate_post_main, NULL, NULL, NULL, NULL);
