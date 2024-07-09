/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "console.h"
#include "multistep_test.h"
#include "panic.h"

#include <stdlib.h>

#include <zephyr/logging/log.h>
#include <zephyr/ztest.h>

LOG_MODULE_REGISTER(ftrapv_hw_test, LOG_LEVEL_INF);

static void test_panic_data(void *fn_addr)
{
	if (IS_ENABLED(CONFIG_ARM)) {
		struct panic_data *const pdata = panic_get_data();
		/* Estimated end of a function. */
		uint32_t fn_end = (uint32_t)fn_addr + 0x40;
		uint32_t pc = pdata->cm.frame[CORTEX_PANIC_FRAME_REGISTER_PC];

		/* Make sure Program Counter is stored correctly and points at a
		 * function that causes a crash. */
		zassert_true(((uint32_t)fn_addr <= pc) && (fn_end >= pc));
	}
}

/*
 * trapping addition: __addvsi3.
 */
static void test_trapv_addition(void)
{
	int32_t test_overflow = INT32_MAX;
	int32_t ret;

	LOG_INF("Testing signed integer addition overflow");
	cflush();
	ret = test_overflow + 1;

	/* Should never reach this. */
	zassert_unreachable();
}

/*
 * trapping subtraction: __subvsi3.
 */
static void test_ftrapv_subtraction(void)
{
	int32_t test_overflow = INT32_MIN;
	int32_t ret;

	test_panic_data(test_trapv_addition);

	LOG_INF("Testing signed integer subtraction overflow");
	cflush();
	ret = test_overflow - 1;

	/* Should never reach this. */
	zassert_unreachable();
}

/*
 * trapping multiplication: __mulvsi3.
 */
static void test_ftrapv_multiplication(void)
{
	int32_t test_overflow = INT32_MAX;
	int32_t ret;

	test_panic_data(test_ftrapv_subtraction);

	LOG_INF("Testing signed integer multiplication overflow");
	cflush();
	ret = test_overflow * 2;

	/* Should never reach this. */
	zassert_unreachable();
}

/*
 * trapping negation: __negvsi2.
 */
static void test_ftrapv_negation(void)
{
	int32_t test_overflow = INT32_MIN;
	int32_t ret;

	test_panic_data(test_ftrapv_multiplication);

	LOG_INF("Testing signed integer negation overflow");
	cflush();
	ret = -test_overflow;

	/* Should never reach this. */
	zassert_unreachable();
}

/*
 * trapping absolute value: __absvsi2.
 *
 * TODO(b/258074414): Trapping on absolute value overflow is broken in clang.
 */
static void test_ftrapv_abs(void)
{
	int32_t test_overflow = INT32_MIN;
	int32_t ret;

	test_panic_data(test_ftrapv_negation);

	LOG_INF("Testing signed integer absolute value overflow\n");
	cflush();

	ret = abs(test_overflow);

	/* Should never reach this. */
	zassert_unreachable();
}

static void test_abs_panic_data(void)
{
	test_panic_data(test_ftrapv_abs);
}

static void (*test_steps[])(void) = { test_trapv_addition,
				      test_ftrapv_subtraction,
				      test_ftrapv_multiplication,
				      test_ftrapv_negation,
				      test_ftrapv_abs,
				      test_abs_panic_data };

MULTISTEP_TEST(ftrapv, test_steps)
