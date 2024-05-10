/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "panic.h"
#include "system.h"
#include "task.h"
#include "test_util.h"

#include <stdlib.h>

/*
 * trapping addition: __addvsi3.
 */
test_static int test_ftrapv_addition(void)
{
	int32_t test_overflow = INT32_MAX;
	int32_t ret;

	ccprintf("Testing signed integer addition overflow\n");
	cflush();
	ret = test_overflow + 1;

	/* Should never reach this. */
	ccprintf("ret: %d\n", ret);
	cflush();

	return EC_ERROR_UNKNOWN;
}

/*
 * trapping subtraction: __subvsi3.
 */
test_static int test_ftrapv_subtraction(void)
{
	int32_t test_overflow = INT32_MIN;
	int32_t ret;

	ccprintf("Testing signed integer subtraction overflow\n");
	cflush();
	ret = test_overflow - 1;

	/* Should never reach this. */
	ccprintf("ret: %d\n", ret);
	cflush();

	return EC_ERROR_UNKNOWN;
}

/*
 * trapping multiplication: __mulvsi3.
 */
test_static int test_ftrapv_multiplication(void)
{
	int32_t test_overflow = INT32_MAX;
	int32_t ret;

	ccprintf("Testing signed integer multiplication overflow\n");
	cflush();
	ret = test_overflow * 2;

	/* Should never reach this. */
	ccprintf("ret: %d\n", ret);
	cflush();

	return EC_ERROR_UNKNOWN;
}

/*
 * trapping negation: __negvsi2.
 */
test_static int test_ftrapv_negation(void)
{
	int32_t test_overflow = INT32_MIN;
	int32_t ret;

	ccprintf("Testing signed integer negation overflow\n");
	cflush();
	ret = -test_overflow;

	/* Should never reach this. */
	ccprintf("ret: %d\n", ret);
	cflush();

	return EC_ERROR_UNKNOWN;
}

/*
 * trapping absolute value: __absvsi2.
 *
 * TODO(b/258074414): Trapping on absolute value overflow is broken in clang.
 */
test_static int test_ftrapv_absolute_value(void)
{
	int32_t test_overflow = INT32_MIN;
	int32_t ret;

	ccprintf("Testing signed integer absolute value overflow\n");
	cflush();

	ret = abs(test_overflow);

	/* Should never reach this. */
	ccprintf("ret: %d\n", ret);
	cflush();

	return EC_ERROR_UNKNOWN;
}

test_static int test_panic_data(void)
{
	const uint32_t expected_reason = 0;
	const uint32_t expected_info = 0;
	/*
	 * https://developer.arm.com/documentation/dui0552/a/the-cortex-m3-processor/exception-model/exception-types
	 */
	const uint8_t expected_exception = 6; /* usage fault */

	uint32_t reason = UINT32_MAX;
	uint32_t info = UINT32_MAX;
	uint8_t exception = UINT8_MAX;

	panic_get_reason(&reason, &info, &exception);

	TEST_EQ(reason, expected_reason, "%08x");
	TEST_EQ(info, expected_info, "%d");
	TEST_EQ(exception, expected_exception, "%d");

	return EC_SUCCESS;
}

test_static void run_test_step1(void)
{
	test_set_next_step(TEST_STATE_STEP_2);
	RUN_TEST(test_ftrapv_addition);
}

test_static void run_test_step2(void)
{
	RUN_TEST(test_panic_data);

	if (test_get_error_count())
		test_reboot_to_next_step(TEST_STATE_FAILED);
	else
		test_reboot_to_next_step(TEST_STATE_STEP_3);
}

test_static void run_test_step3(void)
{
	test_set_next_step(TEST_STATE_STEP_4);
	RUN_TEST(test_ftrapv_subtraction);
}

test_static void run_test_step4(void)
{
	RUN_TEST(test_panic_data);

	if (test_get_error_count())
		test_reboot_to_next_step(TEST_STATE_FAILED);
	else
		test_reboot_to_next_step(TEST_STATE_STEP_5);
}

test_static void run_test_step5(void)
{
	test_set_next_step(TEST_STATE_STEP_6);
	RUN_TEST(test_ftrapv_multiplication);
}

test_static void run_test_step6(void)
{
	RUN_TEST(test_panic_data);

	if (test_get_error_count())
		test_reboot_to_next_step(TEST_STATE_FAILED);
	else
		test_reboot_to_next_step(TEST_STATE_STEP_7);
}

test_static void run_test_step7(void)
{
	test_set_next_step(TEST_STATE_STEP_8);
	RUN_TEST(test_ftrapv_negation);
}

test_static void run_test_step8(void)
{
	RUN_TEST(test_panic_data);

	if (test_get_error_count())
		test_reboot_to_next_step(TEST_STATE_FAILED);
	else
		test_reboot_to_next_step(TEST_STATE_STEP_9);
}

test_static void run_test_step9(void)
{
	/*
	 * TODO(b/258074414): Trapping on absolute value overflow is broken in
	 * clang, so skip the check.
	 */
#if 0
	test_set_next_step(TEST_STATE_STEP_10);
	RUN_TEST(test_ftrapv_absolute_value);
#else
	test_reboot_to_next_step(TEST_STATE_STEP_10);
#endif
}

test_static void run_test_step10(void)
{
	/*
	 * TODO(b/258074414): Trapping on absolute value overflow is broken in
	 * clang, so skip the check.
	 */
#if 0
	RUN_TEST(test_panic_data);
#endif

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
	} else if (state & TEST_STATE_MASK(TEST_STATE_STEP_3)) {
		run_test_step3();
	} else if (state & TEST_STATE_MASK(TEST_STATE_STEP_4)) {
		run_test_step4();
	} else if (state & TEST_STATE_MASK(TEST_STATE_STEP_5)) {
		run_test_step5();
	} else if (state & TEST_STATE_MASK(TEST_STATE_STEP_6)) {
		run_test_step6();
	} else if (state & TEST_STATE_MASK(TEST_STATE_STEP_7)) {
		run_test_step7();
	} else if (state & TEST_STATE_MASK(TEST_STATE_STEP_8)) {
		run_test_step8();
	} else if (state & TEST_STATE_MASK(TEST_STATE_STEP_9)) {
		run_test_step9();
	} else if (state & TEST_STATE_MASK(TEST_STATE_STEP_10)) {
		run_test_step10();
	}
}

int task_test(void *unused)
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
