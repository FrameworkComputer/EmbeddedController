/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "system.h"
#include "test_util.h"

/**
 * The first time this test runs, it should pass. After rebooting, the test
 * should fail because the scratchpad register is set to 1.
 */
test_static int test_scratchpad(void)
{
	int rv;
	uint32_t scratch;

	TEST_EQ(system_get_scratchpad(&scratch), EC_SUCCESS, "%d");
	TEST_EQ(scratch, 0, "%d");

	rv = system_set_scratchpad(1);
	TEST_EQ(rv, EC_SUCCESS, "%d");

	TEST_EQ(system_get_scratchpad(&scratch), EC_SUCCESS, "%d");
	TEST_EQ(scratch, 1, "%d");

	return EC_SUCCESS;
}

void run_test(int argc, const char **argv)
{
	RUN_TEST(test_scratchpad);
	test_print_result();
}
