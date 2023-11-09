/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "console.h"
#include "test_state.h"

#include <zephyr/shell/shell.h>
#include <zephyr/ztest.h>

ZTEST(console_cmd_pwrseq, test_cmd_apreset)
{
	zassert_ok(shell_execute_cmd(get_ec_shell(), "apreset"),
		   "apreset console command failed");
}

ZTEST(console_cmd_pwrseq, test_cmd_apshutdown)
{
	zassert_ok(shell_execute_cmd(get_ec_shell(), "apshutdown"),
		   "apshutdown console command failed");
}

ZTEST_SUITE(console_cmd_pwrseq, ap_power_predicate_post_main, NULL, NULL, NULL,
	    NULL);
