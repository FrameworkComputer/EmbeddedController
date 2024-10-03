/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "system.h"
#include "test_util.h"

static int is_locked;

int system_is_locked(void)
{
	return is_locked;
}

// Smoke test the "fpinfo" console command and its underlying version retrieval.
test_static int test_console_fpinfo()
{
	char console_input[] = "fpinfo";
	TEST_EQ(test_send_console_command(console_input), EC_SUCCESS, "%d");
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

test_static int test_command_fpcapture(void)
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

test_static int test_command_fpenroll(void)
{
	enum ec_error_list res;

	/* System is locked. */
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
		RUN_TEST(test_command_fpupload);
		RUN_TEST(test_command_fpdownload);
		RUN_TEST(test_command_fpmatch);
		RUN_TEST(test_command_fpcapture);
		RUN_TEST(test_command_fpenroll);
	}

	test_print_result();
}
