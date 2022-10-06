/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/**
 * @file
 * @brief Unit Tests for ESPI port 80 console command
 */

#include <zephyr/logging/log.h>
#include <zephyr/shell/shell.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#include "console.h"
#include "ec_commands.h"
#include "port80.h"

#include "test/drivers/test_state.h"
#include "test/drivers/utils.h"

/**
 * @brief TestPurpose: Verify port 80 console commands
 *
 * @details
 * Validate that the port 80 console commands work.
 *
 * Expected Results
 *  - The port 80 console commands return the appropriate result
 */
ZTEST(port80, test_port80_console)
{
	zassert_ok(shell_execute_cmd(get_ec_shell(), "port80"), NULL);
	zassert_ok(shell_execute_cmd(get_ec_shell(), "port80 flush"), NULL);
	zassert_ok(shell_execute_cmd(get_ec_shell(), "port80 scroll"), NULL);
	zassert_ok(shell_execute_cmd(get_ec_shell(), "port80 intprint"), NULL);
	zassert_ok(!shell_execute_cmd(get_ec_shell(), "port80 unknown_param"),
		   NULL);
}

/**
 * @brief Test Suite: Verifies port 80 console commands.
 */
ZTEST_SUITE(console_cmd_port80, drivers_predicate_post_main, NULL, NULL, NULL,
	    NULL);
