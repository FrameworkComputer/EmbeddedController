/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "console.h"
#include "timer.h"

#include <stdio.h>

#include <zephyr/shell/shell.h>
#include <zephyr/ztest.h>

static void test_int(int ms)
{
	char cmd[32];
	unsigned long measured;
	int64_t start;
	int64_t end;

	sprintf(cmd, "waitms %d", ms);
	start = k_uptime_ticks();
	zassert_ok(shell_execute_cmd(get_ec_shell(), cmd),
		   "Failed to execute 'waitms' command");
	end = k_uptime_ticks();
	measured = k_ticks_to_ms_near32(end - start);
	zassert_equal(measured, ms, "'waitms %d' failed, took %ld ms", ms,
		      measured);
}

ZTEST_SUITE(console_cmd_waitms, NULL, NULL, NULL, NULL, NULL);

ZTEST_USER(console_cmd_waitms, test_waitms)
{
	/*
	 * Test across three orders of magnitude. Beyond ~3s the watchdog will
	 * trigger so don't need to bother testing 10s of seconds or greater.
	 */
	test_int(0);
	test_int(5);
	test_int(75);
	test_int(250);
	test_int(1000);

	/* A plain string should fail. */
	zassert_true(shell_execute_cmd(get_ec_shell(), "waitms string"), NULL);

	/* Floats and negative ints should fail. */
	zassert_true(shell_execute_cmd(get_ec_shell(), "waitms 123.456"), NULL);
	zassert_true(shell_execute_cmd(get_ec_shell(), "waitms -67.3"), NULL);
	zassert_true(shell_execute_cmd(get_ec_shell(), "waitms -7"), NULL);
}
