/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "panic.h"
#include "system.h"
#include "task.h"
#include "test_util.h"

#include <exception>

test_static int test_exception()
{
	try {
		ccprintf("Throwing an exception\n");
		throw std::exception();
	} catch (std::exception &e) {
		/*
		 * Since we have exceptions disabled, we should not reach this.
		 * Instead, the exception should cause a reboot.
		 */
		ccprintf("Caught exception\n");
		return EC_ERROR_UNKNOWN;
	}
	return EC_ERROR_UNKNOWN;
}

test_static int test_panic_data()
{
	const uint32_t expected_reason = PANIC_SW_EXIT;
	/* Note: The task_id can be found with the "taskinfo" command. */
	const uint32_t expected_task_id = 5;
	const uint8_t expected_exception = 0;

	uint32_t reason = 0;
	uint32_t info = 0;
	uint8_t exception = UINT8_MAX;

	panic_get_reason(&reason, &info, &exception);

	TEST_EQ(reason, expected_reason, "%08x");
	TEST_EQ(info, expected_task_id, "%d");
	TEST_EQ(exception, expected_exception, "%d");

	return EC_SUCCESS;
}

test_static void run_test_step1()
{
	test_set_next_step(TEST_STATE_STEP_2);
	RUN_TEST(test_exception);
}

test_static void run_test_step2()
{
	RUN_TEST(test_panic_data);

	if (test_get_error_count())
		test_reboot_to_next_step(TEST_STATE_FAILED);
	else
		test_reboot_to_next_step(TEST_STATE_PASSED);
}

void test_run_step(uint32_t state)
{
	if (state & TEST_STATE_MASK(TEST_STATE_STEP_1)) {
		run_test_step1();
	} else if (state & TEST_STATE_MASK(TEST_STATE_STEP_2)) {
		run_test_step2();
	}
}

extern "C" int task_test(void *unused)
{
	if (IS_ENABLED(SECTION_IS_RW))
		test_run_multistep();
	return EC_SUCCESS;
}

void run_test(int argc, const char **argv)
{
	test_reset();
	crec_msleep(30); /* Wait for TASK_ID_TEST to initialize */
	task_wake(TASK_ID_TEST);
}
