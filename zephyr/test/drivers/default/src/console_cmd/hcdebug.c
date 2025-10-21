/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "console.h"
#include "ec_commands.h"
#include "host_command.h"
#include "test/drivers/test_state.h"

#include <zephyr/shell/shell.h>
#include <zephyr/shell/shell_dummy.h>
#include <zephyr/ztest.h>

static void console_cmd_hcdebug_before(void *fixture)
{
	ARG_UNUSED(fixture);
	console_channel_enable("hostcmd");
	zassert_false(console_channel_is_disabled(CC_HOSTCMD));
	shell_execute_cmd(get_ec_shell(), "hcdebug off");
}

static void console_cmd_hcdebug_after(void *fixture)
{
	ARG_UNUSED(fixture);
	console_channel_disable("hostcmd");
	zassert_true(console_channel_is_disabled(CC_HOSTCMD));
	shell_execute_cmd(get_ec_shell(), "hcdebug off");
}

ZTEST_SUITE(console_cmd_hcdebug, drivers_predicate_post_main, NULL,
	    console_cmd_hcdebug_before, console_cmd_hcdebug_after, NULL);

ZTEST_USER(console_cmd_hcdebug, test_too_many_args)
{
	int rv = shell_execute_cmd(get_ec_shell(), "hcdebug arg1 arg2");

	zassert_not_equal(rv, EC_SUCCESS, "Expected %d, but got %d",
			  EC_ERROR_PARAM_COUNT, rv);
}

ZTEST_USER(console_cmd_hcdebug, test_no_args)
{
	zassert_ok(shell_execute_cmd(get_ec_shell(), "hcdebug"), NULL);
}

ZTEST_USER(console_cmd_hcdebug, test_invalid_arg)
{
	int rv = shell_execute_cmd(get_ec_shell(), "hcdebug bar");

	zassert_equal(rv, EC_ERROR_PARAM1, "Expected %d, but got %d",
		      EC_ERROR_PARAM1, rv);
}

ZTEST_USER(console_cmd_hcdebug, test_valid_args)
{
	zassert_ok(shell_execute_cmd(get_ec_shell(), "hcdebug off"), NULL);
	zassert_ok(shell_execute_cmd(get_ec_shell(), "hcdebug normal"), NULL);
	zassert_ok(shell_execute_cmd(get_ec_shell(), "hcdebug every"), NULL);
	zassert_ok(shell_execute_cmd(get_ec_shell(), "hcdebug params"), NULL);
}

/**
 * Executes a host command count times, then verifies the shell output
 * contains the expected strings in the order given.
 * Also verifies the optional unexpected string is not in the output.
 */
static void run_command_and_check_output(struct host_cmd_handler_args *args,
					 int command_count,
					 const char **expected,
					 int expected_count,
					 const char *unexpected)
{
	const char *output;
	size_t output_size;

	shell_backend_dummy_clear_output(get_ec_shell());
	for (int i = 0; i < command_count; i++) {
		int rv = host_command_process(args);
		zassert_ok(rv, "Host command failed, rv=%d", rv);
	}
	cflush();
	output = shell_backend_dummy_get_output(get_ec_shell(), &output_size);
	zassert_not_null(output, "Failed to get shell output");

	if (expected) {
		const char *output_ptr = output;
		for (int i = 0; i < expected_count; i++) {
			output_ptr = strstr(output_ptr, expected[i]);
			zassert_not_null(output_ptr);
		}
	}

	if (unexpected) {
		zassert_is_null(strstr(output, unexpected));
	}
}

ZTEST_USER(console_cmd_hcdebug, test_hcdebug_off)
{
	struct ec_response_hello response;
	struct ec_params_hello params = {
		.in_data = 0x01,
	};
	struct host_cmd_handler_args args =
		BUILD_HOST_COMMAND(EC_CMD_HELLO, 0, response, params);
	const char *unexpected = "HC 0x0001";

	zassert_ok(shell_execute_cmd(get_ec_shell(), "hcdebug off"), NULL);
	run_command_and_check_output(&args, 5, NULL, 0, unexpected);
}

ZTEST_USER(console_cmd_hcdebug, test_hcdebug_normal)
{
	struct ec_response_hello response;
	struct ec_params_hello params = {
		.in_data = 0x01,
	};
	struct host_cmd_handler_args args =
		BUILD_HOST_COMMAND(EC_CMD_HELLO, 0, response, params);
	const char *expected[] = {
		"HC 0x0001", "+", "+", "+", "(++)",
	};
	const char *unexpected = "HC 0x0001.0:01000000";

	zassert_ok(shell_execute_cmd(get_ec_shell(), "hcdebug normal"), NULL);
	run_command_and_check_output(&args, 5, expected, ARRAY_SIZE(expected),
				     unexpected);
}

ZTEST_USER(console_cmd_hcdebug, test_hcdebug_every)
{
	struct ec_response_hello response;
	struct ec_params_hello params = {
		.in_data = 0x01,
	};
	struct host_cmd_handler_args args =
		BUILD_HOST_COMMAND(EC_CMD_HELLO, 0, response, params);
	const char *expected[] = {
		"HC 0x0001", "HC 0x0001", "HC 0x0001", "HC 0x0001", "HC 0x0001",
	};
	const char *unexpected = "HC 0x0001.0:01000000";

	zassert_ok(shell_execute_cmd(get_ec_shell(), "hcdebug every"), NULL);
	run_command_and_check_output(&args, 5, expected, ARRAY_SIZE(expected),
				     unexpected);
}

ZTEST_USER(console_cmd_hcdebug, test_hcdebug_params)
{
	struct ec_response_hello response;
	struct ec_params_hello params = {
		.in_data = 0x01,
	};
	struct host_cmd_handler_args args =
		BUILD_HOST_COMMAND(EC_CMD_HELLO, 0, response, params);
	const char *expected[] = {
		"HC 0x0001.0:01000000", "HC resp:05030201",
		"HC 0x0001.0:01000000", "HC resp:05030201",
		"HC 0x0001.0:01000000", "HC resp:05030201",
		"HC 0x0001.0:01000000", "HC resp:05030201",
		"HC 0x0001.0:01000000", "HC resp:05030201",
	};

	zassert_ok(shell_execute_cmd(get_ec_shell(), "hcdebug params"), NULL);
	run_command_and_check_output(&args, 5, expected, ARRAY_SIZE(expected),
				     NULL);
}
