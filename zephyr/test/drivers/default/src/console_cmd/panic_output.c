/* Copyright 2022 The ChromiumOS Authors.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/shell/shell.h>
#include <zephyr/ztest.h>

#include "console.h"
#include "ec_commands.h"
#include "test/drivers/test_state.h"
#include "test/drivers/utils.h"

/* Test panicinfo when a panic hasn't occurred */
ZTEST_USER(console_cmd_panic_output, test_panicinfo)
{
	zassert_ok(shell_execute_cmd(get_ec_shell(), "panicinfo"),
		   "Failed default print");
}

ZTEST_SUITE(console_cmd_panic_output, NULL, NULL, NULL, NULL, NULL);
