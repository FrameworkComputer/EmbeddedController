/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "debug.h"
#include "string.h"
#include "test_util.h"

static bool debugger_connected;

static void print_usage(void)
{
	ccprintf("usage: runtest [debugger|no_debugger]\n");
}

test_static int test_debugger_is_connected(void)
{
	ccprintf("debugger_is_connected: %d\n", debugger_connected);
	TEST_EQ(debugger_is_connected(), debugger_connected, "%d");
	return EC_SUCCESS;
}

void run_test(int argc, const char **argv)
{
	test_reset();

	if (argc < 2) {
		print_usage();
		test_fail();
		return;
	}

	if (strcmp(argv[1], "debugger") == 0) {
		debugger_connected = true;
	} else if (strcmp(argv[1], "no_debugger") == 0) {
		debugger_connected = false;
	} else {
		print_usage();
		test_fail();
		return;
	}

	RUN_TEST(test_debugger_is_connected);
	test_print_result();
}
