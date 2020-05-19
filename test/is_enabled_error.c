/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Test the IS_ENABLED macro fails on unexpected input.
 */
#include "common.h"
#include "test_util.h"

#define	CONFIG_VALUE		TEST_VALUE

static int test_invalid_value(void)
{
	/* This will cause a compilation error */
	TEST_ASSERT(IS_ENABLED(CONFIG_VALUE) == 0);

	return EC_ERROR_UNKNOWN;
}

void run_test(int argc, char **argv)
{
	test_reset();

	RUN_TEST(test_invalid_value);

	test_print_result();
}
