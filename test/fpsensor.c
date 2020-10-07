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
	test_validate_fp_buffer_offset_success();
	test_validate_fp_buffer_offset_failure_no_overflow();
	test_validate_fp_buffer_offset_failure_overflow();
	test_print_result();
}
