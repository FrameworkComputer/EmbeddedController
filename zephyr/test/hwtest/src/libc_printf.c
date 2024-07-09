/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <stdio.h>

#include <zephyr/ztest.h>

ZTEST_SUITE(libc_printf, NULL, NULL, NULL, NULL, NULL);

ZTEST(libc_printf, test_libc_printf)
{
	/*
	 * The test itself is not verifying that printing works (according to
	 * the test framework, this will always pass).
	 *
	 * Instead, the test runner (test/run_device_tests.py) looks for the
	 * printed string in the test output to determine if the test passed.
	 */
	printf("printf called\n");
}
