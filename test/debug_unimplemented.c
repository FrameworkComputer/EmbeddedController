/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * This test just covers the unimplemented default behavior of these functions.
 * Since these are implemented in the core/cortex-m source code, we can test
 * these on host.
 */

#include "common.h"
#include "debug.h"
#include "test_util.h"

test_static int test_debugger_is_connected(void)
{
	TEST_EQ(debugger_is_connected(), false, "%d");
	return EC_SUCCESS;
}

test_static int test_debugger_was_connected(void)
{
	TEST_EQ(debugger_was_connected(), false, "%d");
	return EC_SUCCESS;
}

void run_test(int argc, const char **argv)
{
	test_reset();
	RUN_TEST(test_debugger_is_connected);
	RUN_TEST(test_debugger_was_connected);
	test_print_result();
}
