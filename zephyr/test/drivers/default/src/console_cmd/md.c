/* Copyright 2022 The ChromiumOS Authors.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/shell/shell.h>
#include <zephyr/ztest.h>

#include "console.h"
#include "test/drivers/test_state.h"

ZTEST_SUITE(console_cmd_md, drivers_predicate_post_main, NULL, NULL, NULL,
	    NULL);

ZTEST_USER(console_cmd_md, test_too_few_args)
{
	zassert_equal(EC_ERROR_PARAM_COUNT,
		      shell_execute_cmd(get_ec_shell(), "md"), NULL);
}

ZTEST_USER(console_cmd_md, test_error_param1)
{
	zassert_equal(EC_ERROR_PARAM1,
		      shell_execute_cmd(get_ec_shell(), "md .j"), NULL);
}

ZTEST_USER(console_cmd_md, test_error_bad_address)
{
	zassert_equal(EC_ERROR_PARAM1,
		      shell_execute_cmd(get_ec_shell(), "md not_an_address"),
		      NULL);
}

ZTEST_USER(console_cmd_md, test_default_count)
{
	uint8_t memory[] = { 0x01, 0x02, 0x03, 0x04 };
	char cmd[128] = { 0 };

	zassume_true(sprintf(cmd, "md %llu", memory) != 0, NULL);
	zassert_ok(shell_execute_cmd(get_ec_shell(), cmd), NULL);
}

ZTEST_USER(console_cmd_md, test_count_arg)
{
	uint8_t memory[] = { 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08 };
	char cmd[128] = { 0 };

	zassume_true(sprintf(cmd, "md %llu 2", memory) != 0, NULL);
	zassert_ok(shell_execute_cmd(get_ec_shell(), cmd), NULL);
}

ZTEST_USER(console_cmd_md, test_byte_format)
{
	uint8_t memory[] = { 0x01, 0x02, 0x03, 0x04 };
	char cmd[128] = { 0 };

	zassume_true(sprintf(cmd, "md .b %llu", memory) != 0, NULL);
	zassert_ok(shell_execute_cmd(get_ec_shell(), cmd), NULL);
}

ZTEST_USER(console_cmd_md, test_half_format)
{
	uint8_t memory[] = { 0x01, 0x02, 0x03, 0x04 };
	char cmd[128] = { 0 };

	zassume_true(sprintf(cmd, "md .h %llu", memory) != 0, NULL);
	zassert_ok(shell_execute_cmd(get_ec_shell(), cmd), NULL);
}

ZTEST_USER(console_cmd_md, test_string_format)
{
	char memory[] = "hello world";
	char cmd[128] = { 0 };

	zassume_true(sprintf(cmd, "md .s %llu 12", memory) != 0, NULL);
	zassert_ok(shell_execute_cmd(get_ec_shell(), cmd), NULL);
}
