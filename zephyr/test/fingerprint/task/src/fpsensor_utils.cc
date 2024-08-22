/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/fff.h>
#include <zephyr/ztest.h>

#include <ec_commands.h>
#include <fpsensor/fpsensor_utils.h>
#include <mkbp_event.h>
#include <rollback.h>

DEFINE_FFF_GLOBALS;

FAKE_VALUE_FUNC(int, mkbp_send_event, uint8_t);

ZTEST_SUITE(fpsensor_utils, NULL, NULL, NULL, NULL, NULL);

enum ec_error_list rollback_get_secret(uint8_t *secret)
{
	// We should not call this function in the test.
	zassert_unreachable();

	return EC_ERROR_UNKNOWN;
}

ZTEST(fpsensor_utils, test_validate_fp_buffer_offset_success)
{
	zassert_equal(validate_fp_buffer_offset(1, 0, 1), EC_SUCCESS);
}

ZTEST(fpsensor_utils, test_validate_fp_buffer_offset_failure_no_overflow)
{
	zassert_equal(validate_fp_buffer_offset(1, 1, 1), EC_ERROR_INVAL);
}

ZTEST(fpsensor_utils, test_validate_fp_buffer_offset_failure_overflow)
{
	zassert_equal(validate_fp_buffer_offset(1, UINT32_MAX, 1),
		      EC_ERROR_OVERFLOW);
}

ZTEST(fpsensor_utils, test_is_test_capture)
{
	zassert_false(is_test_capture(FP_MODE_CAPTURE |
				      FP_CAPTURE_VENDOR_FORMAT
					      << FP_MODE_CAPTURE_TYPE_SHIFT));
	zassert_false(is_test_capture(FP_MODE_CAPTURE |
				      FP_CAPTURE_SIMPLE_IMAGE
					      << FP_MODE_CAPTURE_TYPE_SHIFT));
	zassert_true(is_test_capture(FP_MODE_CAPTURE |
				     FP_CAPTURE_PATTERN0
					     << FP_MODE_CAPTURE_TYPE_SHIFT));
	zassert_true(is_test_capture(FP_MODE_CAPTURE |
				     FP_CAPTURE_PATTERN1
					     << FP_MODE_CAPTURE_TYPE_SHIFT));
	zassert_false(is_test_capture(FP_MODE_CAPTURE |
				      FP_CAPTURE_QUALITY_TEST
					      << FP_MODE_CAPTURE_TYPE_SHIFT));
	zassert_true(is_test_capture(FP_MODE_CAPTURE |
				     FP_CAPTURE_RESET_TEST
					     << FP_MODE_CAPTURE_TYPE_SHIFT));

	/* Check the case where FP_MODE_CAPTURE is not set. */
	zassert_false(is_test_capture(FP_CAPTURE_PATTERN0
				      << FP_MODE_CAPTURE_TYPE_SHIFT));
}

ZTEST(fpsensor_utils, test_is_raw_capture)
{
	zassert_true(is_raw_capture(FP_MODE_CAPTURE |
				    FP_CAPTURE_VENDOR_FORMAT
					    << FP_MODE_CAPTURE_TYPE_SHIFT));
	zassert_false(is_raw_capture(FP_MODE_CAPTURE |
				     FP_CAPTURE_SIMPLE_IMAGE
					     << FP_MODE_CAPTURE_TYPE_SHIFT));
	zassert_false(is_raw_capture(FP_MODE_CAPTURE |
				     FP_CAPTURE_PATTERN0
					     << FP_MODE_CAPTURE_TYPE_SHIFT));
	zassert_false(is_raw_capture(FP_MODE_CAPTURE |
				     FP_CAPTURE_PATTERN1
					     << FP_MODE_CAPTURE_TYPE_SHIFT));
	zassert_true(is_raw_capture(FP_MODE_CAPTURE |
				    FP_CAPTURE_QUALITY_TEST
					    << FP_MODE_CAPTURE_TYPE_SHIFT));
	zassert_false(is_raw_capture(FP_MODE_CAPTURE |
				     FP_CAPTURE_RESET_TEST
					     << FP_MODE_CAPTURE_TYPE_SHIFT));

	/* Check the case where FP_MODE_CAPTURE is not set. */
	zassert_false(is_raw_capture(FP_CAPTURE_VENDOR_FORMAT
				     << FP_MODE_CAPTURE_TYPE_SHIFT));
}
