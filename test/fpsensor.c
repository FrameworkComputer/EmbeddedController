/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "test_util.h"
#include "common/fpsensor/fpsensor_private.h"

test_static int test_validate_fp_buffer_offset_success(void)
{
	TEST_EQ(validate_fp_buffer_offset(1, 0, 1), EC_SUCCESS, "%d");
	return EC_SUCCESS;
}

test_static int test_validate_fp_buffer_offset_failure_no_overflow(void)
{
	TEST_EQ(validate_fp_buffer_offset(1, 1, 1), EC_ERROR_INVAL, "%d");
	return EC_SUCCESS;
}

test_static int test_validate_fp_buffer_offset_failure_overflow(void)
{
	TEST_EQ(validate_fp_buffer_offset(1, UINT32_MAX, 1), EC_ERROR_OVERFLOW,
		"%d");
	return EC_SUCCESS;
}

void run_test(int argc, char **argv)
{
	if (IS_ENABLED(HAS_TASK_FPSENSOR)) {
		/* TODO(b/171924356): The "emulator" build only builds RO and
		 *  the functions used in the tests are only in RW, so these
		 *  tests are not run on the emulator.
		 */
		RUN_TEST(test_validate_fp_buffer_offset_success);
		RUN_TEST(test_validate_fp_buffer_offset_failure_no_overflow);
		RUN_TEST(test_validate_fp_buffer_offset_failure_overflow);
		test_print_result();
		return;
	}
}
