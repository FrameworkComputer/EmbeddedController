/* Copyright 2022 The ChromiumOS Authors
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

	zassert_equal(EC_ERROR_PARAM1,
		      shell_execute_cmd(get_ec_shell(), "rw .j not_an_address"),
		      NULL);
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

	zassume_true(sprintf(cmd, "rw .b %" PRIuPTR, (uintptr_t)memory) != 0,
		     NULL);
	zassert_ok(shell_execute_cmd(get_ec_shell(), cmd), NULL);

	zassume_true(sprintf(cmd, "rw .h %" PRIuPTR, (uintptr_t)memory) != 0,
		     NULL);
	zassert_ok(shell_execute_cmd(get_ec_shell(), cmd), NULL);

	zassume_true(sprintf(cmd, "rw %" PRIuPTR, (uintptr_t)memory) != 0,
		     NULL);
	zassert_ok(shell_execute_cmd(get_ec_shell(), cmd), NULL);
}

ZTEST_USER(console_cmd_rw, test_write_invalid_value)
{
	zassert_equal(EC_ERROR_PARAM2,
		      shell_execute_cmd(get_ec_shell(), "rw 0 not-a-value"),
		      NULL);
	zassert_equal(EC_ERROR_PARAM3,
		      shell_execute_cmd(get_ec_shell(), "rw .b 0 not-a-value"),
		      NULL);
}

ZTEST_USER(console_cmd_rw, test_write)
{
	uint8_t memory[4] = { 0 };
	char cmd[128] = { 0 };

	zassume_true(sprintf(cmd, "rw .b %" PRIuPTR " 1", (uintptr_t)memory) !=
			     0,
		     NULL);
	zassert_ok(shell_execute_cmd(get_ec_shell(), cmd), NULL);
	zassert_equal(1, memory[0], "memory[0] was %u", memory[0]);
	zassert_equal(0, memory[1], "memory[1] was %u", memory[1]);
	zassert_equal(0, memory[2], "memory[2] was %u", memory[2]);
	zassert_equal(0, memory[3], "memory[3] was %u", memory[3]);

	memset(memory, 0, 4);
	zassume_true(sprintf(cmd, "rw .h %" PRIuPTR " 258",
			     (uintptr_t)memory) != 0,
		     NULL);
	zassert_ok(shell_execute_cmd(get_ec_shell(), cmd), NULL);
	zassert_equal(2, memory[0], "memory[0] was %u", memory[0]);
	zassert_equal(1, memory[1], "memory[1] was %u", memory[1]);
	zassert_equal(0, memory[2], "memory[2] was %u", memory[2]);
	zassert_equal(0, memory[3], "memory[3] was %u", memory[3]);

	memset(memory, 0, 4);
	zassume_true(sprintf(cmd, "rw %" PRIuPTR " 16909060",
			     (uintptr_t)memory) != 0,
		     NULL);
	zassert_ok(shell_execute_cmd(get_ec_shell(), cmd), NULL);
	zassert_equal(4, memory[0], "memory[0] was %u", memory[0]);
	zassert_equal(3, memory[1], "memory[1] was %u", memory[1]);
	zassert_equal(2, memory[2], "memory[2] was %u", memory[2]);
	zassert_equal(1, memory[3], "memory[3] was %u", memory[3]);
}
