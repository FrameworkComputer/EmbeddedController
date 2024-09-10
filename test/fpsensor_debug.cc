/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "test_util.h"

// Smoke test the "fpinfo" console command and its underlying version retrieval.
test_static int test_console_fpinfo()
{
	char console_input[] = "fpinfo";
	TEST_EQ(test_send_console_command(console_input), EC_SUCCESS, "%d");
	return EC_SUCCESS;
}

void run_test(int argc, const char **argv)
{
	RUN_TEST(test_console_fpinfo);

	test_print_result();
}
