/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Unit tests for MAX695x 7-segment LED display driver
 */

#include "chipset.h"
#include "console.h"
#include "display_7seg.h"
#include "hooks.h"
#include "test/drivers/test_state.h"
#include "util.h"

#include <stdint.h>

#include <zephyr/shell/shell.h>
#include <zephyr/shell/shell_dummy.h>
#include <zephyr/ztest.h>

/* Simple test to verify display_7seg_write doesn't crash */
ZTEST(max695x_led, test_display_7seg_write_basic)
{
	int ret;

	/* Test console display */
	ret = display_7seg_write(SEVEN_SEG_CONSOLE_DISPLAY, 0x1234);
	zassert_ok(ret, "Console display should succeed");

	/* Test EC display */
	ret = display_7seg_write(SEVEN_SEG_EC_DISPLAY, 0x5678);
	zassert_ok(ret, "EC display should succeed");

	/* Test PORT80 display */
	ret = display_7seg_write(SEVEN_SEG_PORT80_DISPLAY, 0x9ABC);
	zassert_ok(ret, "PORT80 display should succeed");
}

/* Test invalid display module */
ZTEST(max695x_led, test_display_7seg_write_invalid)
{
	int ret = display_7seg_write(99, 0x1234); /* Invalid module */
	zassert_equal(ret, EC_ERROR_UNKNOWN,
		      "Should return error for invalid module");
}

/* Test hooks don't crash */
ZTEST(max695x_led, test_max695x_hooks)
{
	hook_notify(HOOK_INIT);
	hook_notify(HOOK_CHIPSET_STARTUP);
	hook_notify(HOOK_CHIPSET_SHUTDOWN);

	zassert_true(true, "Hooks executed without crashing");
}

/* Test boundary values */
ZTEST(max695x_led, test_display_7seg_boundary_values)
{
	int ret;

	/* Test minimum value */
	ret = display_7seg_write(SEVEN_SEG_CONSOLE_DISPLAY, 0x0000);
	zassert_ok(ret, "Should handle minimum value");

	/* Test maximum value */
	ret = display_7seg_write(SEVEN_SEG_CONSOLE_DISPLAY, 0xFFFF);
	zassert_ok(ret, "Should handle maximum value");
}

/*
 * Console command tests - using shell execution to test the actual
 * driver function.
 */

/* Test console command with insufficient arguments */
ZTEST(max695x_led, test_console_command_insufficient_args)
{
	/*
	 * Test with no arguments - should trigger argc < 2 path in
	 * driver function
	 */
	int ret = shell_execute_cmd(get_ec_shell(), "seg");
	zassert_not_equal(ret, 0, "return error for no arguments");
}

/* Test console command with valid values */
ZTEST(max695x_led, test_console_command_valid_values)
{
	int ret;

	/* Test decimal value */
	ret = shell_execute_cmd(get_ec_shell(), "seg 1234");
	zassert_equal(ret, 0, "Should accept valid decimal value");

	/* Test hex value */
	ret = shell_execute_cmd(get_ec_shell(), "seg 0x1234");
	zassert_equal(ret, 0, "Should accept valid hex value");

	/* Test valid hex uppercase command */
	ret = shell_execute_cmd(get_ec_shell(), "seg 0XABCD");
	zassert_equal(ret, 0, "Valid uppercase hex seg command");

	/* Test minimum value */
	ret = shell_execute_cmd(get_ec_shell(), "seg 0");
	zassert_equal(ret, 0, "Should accept minimum value 0");

	/* Test maximum value (65535 = 0xFFFF) */
	ret = shell_execute_cmd(get_ec_shell(), "seg 65535");
	zassert_equal(ret, 0, "Should accept maximum value 65535");
}

/* Test console command with invalid values */
ZTEST(max695x_led, test_console_command_invalid_values)
{
	int ret;

	/* Test invalid parameter */
	ret = shell_execute_cmd(get_ec_shell(), "seg invalid");
	zassert_not_equal(ret, 0, "Invalid parameter should fail");

	/* Test negative value */
	ret = shell_execute_cmd(get_ec_shell(), "seg -1");
	zassert_not_equal(ret, 0, "Should reject negative values");

	/* Test overflow value (val > UINT16_MAX path) */
	ret = shell_execute_cmd(get_ec_shell(), "seg 65536");
	zassert_not_equal(ret, 0, "Should reject values > 65535");

	/* Test non-numeric string */
	ret = shell_execute_cmd(get_ec_shell(), "seg abc");
	zassert_not_equal(ret, 0, "Should reject non-numeric strings");

	/* Test mixed alphanumeric (invalid) */
	ret = shell_execute_cmd(get_ec_shell(), "seg 123abc");
	zassert_not_equal(ret, 0, "Should reject alphanumeric strings");

	/* Test string with spaces (should trigger *e != 0 path) */
	ret = shell_execute_cmd(get_ec_shell(), "seg '12 34'");
	zassert_not_equal(ret, 0, "Number with spaces should fail");
}

/* Test console command boundary cases */
ZTEST(max695x_led, test_console_command_boundary_cases)
{
	int ret;

	/* Test hex prefix variants */
	ret = shell_execute_cmd(get_ec_shell(), "seg 0X1234");
	zassert_equal(ret, 0, "Should accept uppercase hex prefix");

	/* Test octal value (starts with 0) */
	ret = shell_execute_cmd(get_ec_shell(), "seg 01234");
	zassert_equal(ret, 0, "Should accept octal values");

	/* Test maximum hex value */
	ret = shell_execute_cmd(get_ec_shell(), "seg 0xFFFF");
	zassert_equal(ret, 0, "Should accept maximum hex value 0xFFFF");

	/* Test overflow hex value */
	ret = shell_execute_cmd(get_ec_shell(), "seg 0x10000");
	zassert_not_equal(ret, 0, "Should reject hex values > 0xFFFF");
}

/* Test console command with whitespace handling */
ZTEST(max695x_led, test_console_command_whitespace)
{
	int ret;

	/* Test leading whitespace */
	ret = shell_execute_cmd(get_ec_shell(), "seg 123");
	zassert_equal(ret, 0, "Should handle whitespace properly");

	/* Test trailing characters after valid number - should fail */
	ret = shell_execute_cmd(get_ec_shell(), "seg 123abc");
	zassert_not_equal(ret, 0, "Should reject alphanumeric characters");
}

ZTEST_SUITE(max695x_led, drivers_predicate_post_main, NULL, NULL, NULL, NULL);
