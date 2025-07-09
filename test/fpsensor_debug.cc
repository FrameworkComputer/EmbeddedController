/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "system.h"
#include "test_util.h"

#if defined(SECTION_IS_RW)
#include "fpsensor/fpsensor_state_driver.h"
#include "fpsensor_driver.h"
#endif

#include <stdio.h>

#include <algorithm>

static int is_locked;

int system_is_locked(void)
{
	return is_locked;
}

// Smoke test the "fpinfo" console command and its underlying version retrieval.
test_static int test_console_fpinfo()
{
	/* System is unlocked. */
	is_locked = 0;

	char console_input[] = "fpinfo";
	TEST_EQ(test_send_console_command(console_input), EC_SUCCESS, "%d");
	return EC_SUCCESS;
}

test_static int test_command_fpupload_success(void)
{
	/* System is unlocked. */
	is_locked = 0;

	char console_input[] = "fpupload 52 image";
	enum ec_error_list res = test_send_console_command(console_input);
	TEST_EQ(res, EC_SUCCESS, "%d");

	return EC_SUCCESS;
}

test_static int test_command_fpupload_system_is_locked(void)
{
	/* System is locked. */
	is_locked = 1;

	/* Test for the case when access is denied. */
	char console_input[] = "fpupload 52 image";
	enum ec_error_list res = test_send_console_command(console_input);
	TEST_EQ(res, EC_ERROR_ACCESS_DENIED, "%d");

	return EC_SUCCESS;
}

test_static int test_command_fpupload_one_argument(void)
{
	/* System is unlocked. */
	is_locked = 0;

	char console_input[] = "fpupload 52";
	enum ec_error_list res = test_send_console_command(console_input);
	TEST_EQ(res, EC_ERROR_PARAM_COUNT, "%d");

	return EC_SUCCESS;
}

test_static int test_command_fpupload_three_arguments(void)
{
	/* System is unlocked. */
	is_locked = 0;

	char console_input[] = "fpupload 52 image 76";
	enum ec_error_list res = test_send_console_command(console_input);
	TEST_EQ(res, EC_ERROR_PARAM_COUNT, "%d");

	return EC_SUCCESS;
}

test_static int test_command_fpupload_negative_offset(void)
{
	/* System is unlocked. */
	is_locked = 0;

	char console_input[] = "fpupload -1 image";
	enum ec_error_list res = test_send_console_command(console_input);
	TEST_EQ(res, EC_ERROR_PARAM1, "%d");

	return EC_SUCCESS;
}

test_static int
test_command_fpupload_offset_equal_image_size_minus_image_offset(void)
{
	/* System is unlocked. */
	is_locked = 0;

#if defined(SECTION_IS_RW)
	char console_input[] = "fpupload " STRINGIFY(UINT32_MAX) " image";
	snprintf(console_input, sizeof(console_input),
		 "fpupload %" PRIu32 " image",
		 FP_SENSOR_IMAGE_SIZE - FP_SENSOR_IMAGE_OFFSET);
	enum ec_error_list res = test_send_console_command(console_input);
	TEST_EQ(res, EC_ERROR_PARAM1, "%d");
#endif

	return EC_SUCCESS;
}

test_static int test_command_fpupload_correct_uploaded_values(void)
{
	/* System is unlocked. */
	is_locked = 0;

#if defined(SECTION_IS_RW)
	std::ranges::fill(fp_buffer, fp_buffer + FP_SENSOR_IMAGE_SIZE, 0);
	char console_input[] = "fpupload 0 f16e38";
	enum ec_error_list res = test_send_console_command(console_input);
	TEST_EQ(res, EC_SUCCESS, "%d");
	TEST_EQ(fp_buffer[FP_SENSOR_IMAGE_OFFSET], 241, "%d");
	TEST_EQ(fp_buffer[FP_SENSOR_IMAGE_OFFSET + 1], 110, "%d");
	TEST_EQ(fp_buffer[FP_SENSOR_IMAGE_OFFSET + 2], 56, "%d");

#endif

	return EC_SUCCESS;
}

test_static int test_command_fpdownload(void)
{
	enum ec_error_list res;

	/* System is unlocked. */
	is_locked = 0;

	char console_input1[] = "fpdownload";
	res = test_send_console_command(console_input1);
	TEST_EQ(res, EC_SUCCESS, "%d");

	/* System is locked. */
	is_locked = 1;

	/* Test for the case when access is denied. */
	char console_input2[] = "fpdownload";
	res = test_send_console_command(console_input2);
	TEST_EQ(res, EC_ERROR_ACCESS_DENIED, "%d");

	return EC_SUCCESS;
}

test_static int test_command_fpmatch(void)
{
	enum ec_error_list res;

	/* System is locked. */
	is_locked = 1;

	/* Test for the case when access is denied. */
	char console_input2[] = "fpmatch";
	res = test_send_console_command(console_input2);
	TEST_EQ(res, EC_ERROR_ACCESS_DENIED, "%d");

	return EC_SUCCESS;
}

test_static int test_command_fpcapture_system_is_locked(void)
{
	enum ec_error_list res;

	/* System is locked. */
	is_locked = 1;

	/* Test for the case when access is denied. */
	char console_input[] = "fpcapture";
	res = test_send_console_command(console_input);
	TEST_EQ(res, EC_ERROR_ACCESS_DENIED, "%d");

	return EC_SUCCESS;
}

test_static int test_command_fpcapture_mode_is_negative(void)
{
	enum ec_error_list res;

	/* System is unlocked. */
	is_locked = 0;

	/* Test for the case when access capture mode is negative. */
	char console_input[] = "fpcapture -1";
	res = test_send_console_command(console_input);
	TEST_EQ(res, EC_ERROR_PARAM1, "%d");

	return EC_SUCCESS;
}

test_static int test_command_fpcapture_mode_is_too_large(void)
{
	enum ec_error_list res;

	/* System is unlocked. */
	is_locked = 0;

	/* Test for the case when capture mode is larger than
	 * FP_CAPTURE_TYPE_MAX.
	 */
	char console_input[] = "fpcapture 56";
	snprintf(console_input, sizeof(console_input), "fpcapture %d",
		 FP_CAPTURE_TYPE_MAX);
	res = test_send_console_command(console_input);
	TEST_EQ(res, EC_ERROR_PARAM1, "%d");

	return EC_SUCCESS;
}

test_static int test_command_fpenroll(void)
{
	enum ec_error_list res;

	/* System is unlocked. */
	is_locked = 1;

	/* Test for the case when access is denied. */
	char console_input[] = "fpenroll";
	res = test_send_console_command(console_input);
	TEST_EQ(res, EC_ERROR_ACCESS_DENIED, "%d");

	return EC_SUCCESS;
}

void run_test(int argc, const char **argv)
{
	test_reset();

	RUN_TEST(test_console_fpinfo);
	if (!IS_ENABLED(BOARD_HOST)) {
		RUN_TEST(test_command_fpupload_success);
		RUN_TEST(test_command_fpupload_system_is_locked);
		RUN_TEST(test_command_fpupload_one_argument);
		RUN_TEST(test_command_fpupload_three_arguments);
		RUN_TEST(test_command_fpupload_negative_offset);
		RUN_TEST(
			test_command_fpupload_offset_equal_image_size_minus_image_offset);
		RUN_TEST(test_command_fpupload_correct_uploaded_values);
		RUN_TEST(test_command_fpdownload);
		RUN_TEST(test_command_fpmatch);
		RUN_TEST(test_command_fpcapture_system_is_locked);
		RUN_TEST(test_command_fpcapture_mode_is_negative);
		RUN_TEST(test_command_fpcapture_mode_is_too_large);
		RUN_TEST(test_command_fpenroll);
	}

	test_print_result();
}
