/* Copyright 2022 The ChromiumOS Authors.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/shell/shell.h>
#include <zephyr/ztest.h>

#include "console.h"
#include "ec_commands.h"
#include "panic.h"
#include "test/drivers/test_state.h"
#include "test/drivers/utils.h"

/* Test panicinfo when a panic hasn't occurred */
ZTEST_USER(console_cmd_panic_output, test_panicinfo)
{
	zassert_ok(shell_execute_cmd(get_ec_shell(), "panicinfo"),
		   "Failed default print");
}

/* Test panicinfo when a panic hasn't occurred with an extra arg. */
/* Should return successfully. */
ZTEST_USER(console_cmd_panic_output, test_panicinfo_bad_arg)
{
	zassert_ok(shell_execute_cmd(get_ec_shell(), "panicinfo fish"),
		   "Failed default print with a bad argument");
}

/* Fixture needed to save panic data state */
struct console_cmd_panic_output_fixture {
	struct panic_data *p_data;
	struct panic_data cpy_data;
};

static void *console_cmd_panic_setup(void)
{
	static struct console_cmd_panic_output_fixture fixture;

	return &fixture;
}

static void console_cmd_panic_before(void *data)
{
	struct console_cmd_panic_output_fixture *fixture = data;

	fixture->p_data = get_panic_data_write();
	fixture->cpy_data = *fixture->p_data;
}

static void console_cmd_panic_after(void *data)
{
	struct console_cmd_panic_output_fixture *fixture = data;

	struct panic_data *p_data = fixture->p_data;

	*p_data = fixture->cpy_data;
}

/* Tests that need the fixture */
ZTEST_USER_F(console_cmd_panic_output, test_panicinfo_with_panic)
{
	fixture->p_data->flags = 0x1;
	fixture->p_data->struct_size = CONFIG_PANIC_DATA_SIZE;
	fixture->p_data->magic = PANIC_DATA_MAGIC;
	zassert_ok(shell_execute_cmd(get_ec_shell(), "panicinfo"),
		   "Failed to print details about panic.");
}

ZTEST_SUITE(console_cmd_panic_output, NULL, console_cmd_panic_setup,
	    console_cmd_panic_before, console_cmd_panic_after, NULL);
