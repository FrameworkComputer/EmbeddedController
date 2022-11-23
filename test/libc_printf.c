/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "test_util.h"

#include <stdio.h>

test_static int test_printf(void)
{
	/*
	 * The test itself is not verifying that printing works (according to
	 * the test framework, this will always pass).
	 *
	 * Instead, the test runner (test/run_device_tests.py) looks for the
	 * printed string in the test output to determine if the test passed.
	 */
	printf("printf called\n");
	return EC_SUCCESS;
}

void run_test(int argc, const char **argv)
{
	test_reset();
	RUN_TEST(test_printf);
	test_print_result();
}
