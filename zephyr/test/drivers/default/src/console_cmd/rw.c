/* Copyright 2022 The ChromiumOS Authors.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/shell/shell.h>
#include <zephyr/ztest.h>

#include "console.h"
#include "test/drivers/test_state.h"

ZTEST_SUITE(console_cmd_rw, drivers_predicate_post_main, NULL, NULL, NULL,
	    NULL);

ZTEST_USER(console_cmd_rw, test_too_few_args)
{
	zassert_equal(EC_ERROR_PARAM_COUNT,
		      shell_execute_cmd(get_ec_shell(), "rw"), NULL);
}

ZTEST_USER(console_cmd_rw, test_error_param1)
{
	zassert_equal(EC_ERROR_PARAM1,
		      shell_execute_cmd(get_ec_shell(), "rw .j"), NULL);
}

ZTEST_USER(console_cmd_rw, test_error_bad_address)
{
	zassert_equal(EC_ERROR_PARAM1,
		      shell_execute_cmd(get_ec_shell(), "rw not_an_address"),
		      NULL);
	zassert_equal(EC_ERROR_PARAM2,
		      shell_execute_cmd(get_ec_shell(), "rw .b not_an_address"),
		      NULL);
}

ZTEST_USER(console_cmd_rw, test_read)
{
	uint8_t memory[] = { 0x01, 0x02, 0x03, 0x04 };
	char cmd[128] = { 0 };

	zassume_true(sprintf(cmd, "rw .b %llu", memory) != 0, NULL);
	zassert_ok(shell_execute_cmd(get_ec_shell(), cmd), NULL);

	zassume_true(sprintf(cmd, "rw .h %llu", memory) != 0, NULL);
	zassert_ok(shell_execute_cmd(get_ec_shell(), cmd), NULL);

	zassume_true(sprintf(cmd, "rw %llu", memory) != 0, NULL);
	zassert_ok(shell_execute_cmd(get_ec_shell(), cmd), NULL);
}
