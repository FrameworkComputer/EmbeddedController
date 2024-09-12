/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "test_util.h"

#include <stdio.h>

static int is_locked;

int system_is_locked(void)
{
	return is_locked;
}

test_static int test_command_mem_dump(void)
{
	enum ec_error_list res;
	/* This word will be read by the md command. */
	const volatile uint32_t valid_word = 0x1badd00d;
	/* Compose the md console command to read |valid_word|. */
	char console_input[] = "md 0x01234567";

	snprintf(console_input, sizeof(console_input), "md %p", &valid_word);

	is_locked = 0;
	res = test_send_console_command(console_input);
	TEST_EQ(res, EC_SUCCESS, "%d");

	is_locked = 1;
	res = test_send_console_command(console_input);
	TEST_EQ(res, EC_ERROR_ACCESS_DENIED, "%d");

	return EC_SUCCESS;
}

test_static int test_command_read_write_word(void)
{
	enum ec_error_list res;
	const uint32_t old_value = 0x1badd00d;
	/* This word will be read/written by the rw command. */
	volatile uint32_t valid_word = old_value;
	/* Compose the rw console command to write |valid_word| with a value
	 * of 5.
	 */
	char console_input[] = "rw 0x01234567 0x05";
	const uint32_t new_value = 0x05;

	snprintf(console_input, sizeof(console_input), "rw %p 0x%02x",
		 &valid_word, new_value);

	is_locked = 0;
	res = test_send_console_command(console_input);
	TEST_EQ(res, EC_SUCCESS, "%d");
	TEST_EQ(new_value, valid_word, "%" PRIu32);

	is_locked = 1;
	/* Reset valid word */
	valid_word = old_value;
	res = test_send_console_command(console_input);
	TEST_EQ(res, EC_ERROR_ACCESS_DENIED, "%d");
	TEST_EQ(old_value, valid_word, "%" PRIu32);

	return EC_SUCCESS;
}

test_static int test_command_fpupload(void)
{
	/* System is unlocked. */
	is_locked = 0;

	char console_input1[] = "fpupload 52 image";
	enum ec_error_list res = test_send_console_command(console_input1);
	TEST_EQ(res, EC_SUCCESS, "%d");

	/* System is locked. */
	is_locked = 1;

	/* Test for the case when access is denied. */
	char console_input2[] = "fpupload 52 image";
	res = test_send_console_command(console_input2);
	TEST_EQ(res, EC_ERROR_ACCESS_DENIED, "%d");

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

void run_test(int argc, const char **argv)
{
	test_reset();

	RUN_TEST(test_command_mem_dump);
	RUN_TEST(test_command_read_write_word);
	RUN_TEST(test_command_fpupload);
	RUN_TEST(test_command_fpdownload);
	RUN_TEST(test_command_fpmatch);

	test_print_result();
}
