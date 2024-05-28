/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "fpsensor/fpsensor_utils.h"
#include "rollback.h"
#include "test_util.h"

enum ec_error_list rollback_get_secret(uint8_t *secret)
{
	// We should not call this function in the test.
	TEST_ASSERT(false);
}

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

test_static int test_is_test_capture()
{
	TEST_ASSERT(!is_test_capture(FP_MODE_CAPTURE |
				     FP_CAPTURE_VENDOR_FORMAT
					     << FP_MODE_CAPTURE_TYPE_SHIFT));
	TEST_ASSERT(!is_test_capture(FP_MODE_CAPTURE |
				     FP_CAPTURE_SIMPLE_IMAGE
					     << FP_MODE_CAPTURE_TYPE_SHIFT));
	TEST_ASSERT(is_test_capture(FP_MODE_CAPTURE |
				    FP_CAPTURE_PATTERN0
					    << FP_MODE_CAPTURE_TYPE_SHIFT));
	TEST_ASSERT(is_test_capture(FP_MODE_CAPTURE |
				    FP_CAPTURE_PATTERN1
					    << FP_MODE_CAPTURE_TYPE_SHIFT));
	TEST_ASSERT(!is_test_capture(FP_MODE_CAPTURE |
				     FP_CAPTURE_QUALITY_TEST
					     << FP_MODE_CAPTURE_TYPE_SHIFT));
	TEST_ASSERT(is_test_capture(FP_MODE_CAPTURE |
				    FP_CAPTURE_RESET_TEST
					    << FP_MODE_CAPTURE_TYPE_SHIFT));

	/* Check the case where FP_MODE_CAPTURE is not set. */
	TEST_ASSERT(!is_test_capture(FP_CAPTURE_PATTERN0
				     << FP_MODE_CAPTURE_TYPE_SHIFT));

	return EC_SUCCESS;
}

test_static int test_is_raw_capture()
{
	TEST_ASSERT(is_raw_capture(FP_MODE_CAPTURE |
				   FP_CAPTURE_VENDOR_FORMAT
					   << FP_MODE_CAPTURE_TYPE_SHIFT));
	TEST_ASSERT(!is_raw_capture(FP_MODE_CAPTURE |
				    FP_CAPTURE_SIMPLE_IMAGE
					    << FP_MODE_CAPTURE_TYPE_SHIFT));
	TEST_ASSERT(!is_raw_capture(FP_MODE_CAPTURE |
				    FP_CAPTURE_PATTERN0
					    << FP_MODE_CAPTURE_TYPE_SHIFT));
	TEST_ASSERT(!is_raw_capture(FP_MODE_CAPTURE |
				    FP_CAPTURE_PATTERN1
					    << FP_MODE_CAPTURE_TYPE_SHIFT));
	TEST_ASSERT(is_raw_capture(FP_MODE_CAPTURE |
				   FP_CAPTURE_QUALITY_TEST
					   << FP_MODE_CAPTURE_TYPE_SHIFT));
	TEST_ASSERT(!is_raw_capture(FP_MODE_CAPTURE |
				    FP_CAPTURE_RESET_TEST
					    << FP_MODE_CAPTURE_TYPE_SHIFT));

	/* Check the case where FP_MODE_CAPTURE is not set. */
	TEST_ASSERT(!is_raw_capture(FP_CAPTURE_VENDOR_FORMAT
				    << FP_MODE_CAPTURE_TYPE_SHIFT));

	return EC_SUCCESS;
}

void run_test(int argc, const char **argv)
{
	RUN_TEST(test_validate_fp_buffer_offset_success);
	RUN_TEST(test_validate_fp_buffer_offset_failure_no_overflow);
	RUN_TEST(test_validate_fp_buffer_offset_failure_overflow);

	RUN_TEST(test_is_test_capture);
	RUN_TEST(test_is_raw_capture);

	test_print_result();
}
