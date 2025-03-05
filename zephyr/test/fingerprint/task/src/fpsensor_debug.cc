/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "common.h"
#include "console.h"
#include "system.h"

#include <stdio.h>

#include <zephyr/fff.h>
#include <zephyr/ztest.h>

#include <algorithm>
#include <array>
#include <ec_commands.h>
#include <fpsensor/fpsensor_state_driver.h>
#include <fpsensor/fpsensor_utils.h>
#include <fpsensor_driver.h>
#include <mkbp_event.h>
#include <rollback.h>

static int is_locked;

int system_is_locked(void)
{
	return is_locked;
}

DEFINE_FFF_GLOBALS;

FAKE_VALUE_FUNC(int, mkbp_send_event, uint8_t);

ZTEST_SUITE(fpsensor_debug, NULL, NULL, NULL, NULL, NULL);

ZTEST(fpsensor_debug, test_console_fpinfo)
{
	/* System is unlocked. */
	is_locked = 0;

	char console_input[] = "fpinfo";
	int rv = shell_execute_cmd(get_ec_shell(), console_input);
	zassert_equal(rv, EC_SUCCESS);
}

/* TODO(b/371647536): Add other tests of commands in fpsensor_debug to verify
 * entire handlers.
 */
ZTEST(fpsensor_debug, test_command_fpupload_success)
{
	/* System is unlocked. */
	is_locked = 0;

	char console_input[] = "fpupload 52 image";
	int rv = shell_execute_cmd(get_ec_shell(), console_input);
	zassert_equal(rv, EC_SUCCESS);
}

ZTEST(fpsensor_debug, test_command_fpupload_system_is_locked)
{
	/* System is locked. */
	is_locked = 1;

	/* Test for the case when access is denied. */
	char console_input[] = "fpupload 52 image";
	int rv = shell_execute_cmd(get_ec_shell(), console_input);
	zassert_equal(rv, EC_ERROR_ACCESS_DENIED);
}

ZTEST(fpsensor_debug, test_command_fpupload_one_argument)
{
	/* System is unlocked. */
	is_locked = 0;

	char console_input[] = "fpupload 52";
	int rv = shell_execute_cmd(get_ec_shell(), console_input);
	zassert_equal(rv, EC_ERROR_PARAM_COUNT);
}

ZTEST(fpsensor_debug, test_command_fpupload_three_arguments)
{
	/* System is unlocked. */
	is_locked = 0;

	char console_input[] = "fpupload 52 image 78";
	int rv = shell_execute_cmd(get_ec_shell(), console_input);
	zassert_equal(rv, EC_ERROR_PARAM_COUNT);
}

ZTEST(fpsensor_debug, test_command_fpupload_negative_offset)
{
	/* System is unlocked. */
	is_locked = 0;

	char console_input[] = "fpupload -1 image";
	int rv = shell_execute_cmd(get_ec_shell(), console_input);
	zassert_equal(rv, EC_ERROR_PARAM1);
}

ZTEST(fpsensor_debug,
      test_command_fpupload_offset_equal_image_size_minus_image_offset)

{
	/* System is unlocked. */
	is_locked = 0;

	char console_input[] = "fpupload " STRINGIFY(UINT32_MAX) " image";
	snprintf(console_input, sizeof(console_input),
		 "fpupload %" PRIu32 " image",
		 FP_SENSOR_IMAGE_SIZE - FP_SENSOR_IMAGE_OFFSET);
	int rv = shell_execute_cmd(get_ec_shell(), console_input);
	zassert_equal(rv, EC_ERROR_PARAM1);
}

ZTEST(fpsensor_debug, test_command_fpupload_correct_uploaded_values)
{
	/* System is unlocked. */
	is_locked = 0;

	std::ranges::fill(fp_buffer, fp_buffer + FP_SENSOR_IMAGE_SIZE, 0);
	char console_input[] = "fpupload 0 f16e38";
	int rv = shell_execute_cmd(get_ec_shell(), console_input);
	zassert_equal(rv, EC_SUCCESS);
	zassert_equal(fp_buffer[FP_SENSOR_IMAGE_OFFSET], 241);
	zassert_equal(fp_buffer[FP_SENSOR_IMAGE_OFFSET + 1], 110);
	zassert_equal(fp_buffer[FP_SENSOR_IMAGE_OFFSET + 2], 56);
}

/* TODO(b/371647536): Add other tests of commands in fpsensor_debug to verify
 * entire handlers.
 */
ZTEST(fpsensor_debug, test_command_fpcapture_system_is_locked)
{
	/* System is locked. */
	is_locked = 1;

	char console_input[] = "fpcapture";
	int rv = shell_execute_cmd(get_ec_shell(), console_input);
	zassert_equal(rv, EC_ERROR_ACCESS_DENIED);
}

ZTEST(fpsensor_debug, test_command_fpcapture_mode_is_negative)
{
	/* System is unlocked. */
	is_locked = 0;

	char console_input[] = "fpcapture -1";
	int rv = shell_execute_cmd(get_ec_shell(), console_input);
	zassert_equal(rv, EC_ERROR_PARAM1);
}

ZTEST(fpsensor_debug, test_command_fpcapture_mode_is_too_large)
{
	/* System is unlocked. */
	is_locked = 0;

	char console_input[] = "fpcapture 56";
	snprintf(console_input, sizeof(console_input), "fpcapture %d",
		 FP_CAPTURE_TYPE_MAX);
	int rv = shell_execute_cmd(get_ec_shell(), console_input);
	zassert_equal(rv, EC_ERROR_PARAM1);
}

/* TODO(b/371647536): Add other tests of commands in fpsensor_debug to verify
 * entire handlers.
 */
ZTEST(fpsensor_debug, test_command_fpenroll)
{
	/* System is locked. */
	is_locked = 1;

	char console_input[] = "fpenroll";
	int rv = shell_execute_cmd(get_ec_shell(), console_input);
	zassert_equal(rv, EC_ERROR_ACCESS_DENIED);
}

enum ec_error_list upload_pgm_image(uint8_t *frame, uint8_t bpp);

ZTEST(fpsensor_debug, test_upload_pgm_image_wrong_bpp)
{
	std::array<uint8_t, 100> frame{};

	zassert_equal(upload_pgm_image(frame.data(), 0), EC_ERROR_UNKNOWN);
	zassert_equal(upload_pgm_image(frame.data(), 17), EC_ERROR_UNKNOWN);
	zassert_equal(upload_pgm_image(frame.data(), 24), EC_ERROR_UNKNOWN);
}

uint8_t get_sensor_bpp(void);

ZTEST(fpsensor_debug, test_get_sensor_bpp)
{
	zassert_equal(get_sensor_bpp(), 8);
}
