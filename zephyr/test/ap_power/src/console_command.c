/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/shell/shell.h>
#include <zephyr/ztest.h>

#include "console.h"
#include "ec_commands.h"
#include "test_state.h"

ZTEST_USER(console_cmd_debug_mode, test_debug_mode_default)
{
	zassert_ok(shell_execute_cmd(get_ec_shell(), "debug_mode"),
		   "failed to get debug_mode");
}

ZTEST_USER(console_cmd_debug_mode, test_debug_mode_disabled)
{
	zassert_ok(shell_execute_cmd(get_ec_shell(), "debug_mode disable"),
		   "failed to disable debug_mode");
}

ZTEST_USER(console_cmd_debug_mode, test_debug_mode_enabled)
{
	zassert_ok(shell_execute_cmd(get_ec_shell(), "debug_mode enable"),
		   "failed to enable debug_mode");
}

ZTEST_SUITE(console_cmd_debug_mode, ap_power_predicate_post_main, NULL, NULL,
	    NULL, NULL);
