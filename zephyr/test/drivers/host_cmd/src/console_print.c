/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "console.h"
#include "ec_commands.h"
#include "test/drivers/stubs.h"
#include "test/drivers/test_state.h"
#include "test/drivers/utils.h"

#include <zephyr/shell/shell_dummy.h>
#include <zephyr/ztest.h>

#define HOST_MSG_PREFIX "Host: "

static void run_command_and_check_output(struct host_cmd_handler_args *args,
					 const char *expected)
{
	int rv;
	size_t output_size;
	const char *output;
	const char *output_ptr;
	const struct shell *shell_zephyr = get_ec_shell();

	shell_backend_dummy_clear_output(shell_zephyr);

	rv = host_command_process(args);
	zassert_ok(rv, "Got %d", rv);

	output = shell_backend_dummy_get_output(shell_zephyr, &output_size);
	zassert_not_null(output, "Failed to get shell output");
	zassert_true(output_size > 0, "Failed to get shell output");

	/* Output should look like '[<time> Host: <msg>]' */
	output_ptr = output;
	zassert_equal('[', output_ptr[0], "Missing opening bracket output: %s",
		      output);
	output_ptr += 1;
	output_ptr = strstr(output_ptr, HOST_MSG_PREFIX);
	zassert_not_null(output_ptr,
			 "Missing host message prefix (%s) in output: %s",
			 HOST_MSG_PREFIX, output);
	output_ptr += strlen(HOST_MSG_PREFIX);
	output_ptr = strstr(output_ptr, expected);
	zassert_not_null(output_ptr,
			 "Missing expected message (%s) in output: %s",
			 expected, output);
	output_ptr += strlen(expected);
	zassert_equal(']', output_ptr[0],
		      "Missing closing bracket in output: %s", output);
}

ZTEST_USER(host_cmd_console_print, test_early_terminated_message)
{
	const char msg[] = "Early\x00 Termination";
	const char expected[] = "Early";
	struct host_cmd_handler_args args =
		BUILD_HOST_COMMAND_PARAMS(EC_CMD_CONSOLE_PRINT, 0, msg);

	run_command_and_check_output(&args, expected);
}

ZTEST_USER(host_cmd_console_print, test_unterminated_message)
{
	const char msg[] = { 'U', 'n', 't', 'e', 'r', 'm', 'i',
			     'n', 'a', 't', 'e', 'd', '!' };
	const char expected[] = "Unterminated";
	struct host_cmd_handler_args args =
		BUILD_HOST_COMMAND_PARAMS(EC_CMD_CONSOLE_PRINT, 0, msg);

	run_command_and_check_output(&args, expected);
}

ZTEST_USER(host_cmd_console_print, test_long_message)
{
	/* 20 bytes is needed for timestamp, prefix and brackets */
	char msg[CONFIG_SHELL_PRINTF_BUFF_SIZE - 20];

	memset(msg, 'x', sizeof(msg) - 1);
	msg[sizeof(msg) - 1] = '\0';

	struct host_cmd_handler_args args =
		BUILD_HOST_COMMAND_PARAMS(EC_CMD_CONSOLE_PRINT, 0, msg);

	run_command_and_check_output(&args, msg);
}

ZTEST_USER(host_cmd_console_print, test_short_message)
{
	const char msg[] = "Hello, EC!";
	struct host_cmd_handler_args args =
		BUILD_HOST_COMMAND_PARAMS(EC_CMD_CONSOLE_PRINT, 0, msg);

	run_command_and_check_output(&args, msg);
}

ZTEST_USER(host_cmd_console_print, test_empty_message)
{
	const char msg[] = "";
	struct host_cmd_handler_args args =
		BUILD_HOST_COMMAND_PARAMS(EC_CMD_CONSOLE_PRINT, 0, msg);

	run_command_and_check_output(&args, msg);
}

ZTEST_USER(host_cmd_console_print, test_no_message)
{
	int rv;

	struct host_cmd_handler_args args =
		BUILD_HOST_COMMAND_PARAMS(EC_CMD_CONSOLE_PRINT, 0, EMPTY);

	rv = host_command_process(&args);
	zassert_equal(EC_RES_INVALID_PARAM, rv, "Got %d", rv);
}

ZTEST_SUITE(host_cmd_console_print, drivers_predicate_post_main, NULL, NULL,
	    NULL, NULL);
