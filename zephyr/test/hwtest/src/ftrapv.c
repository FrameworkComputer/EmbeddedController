/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "console.h"

#include <stdlib.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/ztest.h>
#include <zephyr/ztest_error_hook.h>

LOG_MODULE_REGISTER(ftrapv_hw_test, LOG_LEVEL_INF);

static uintptr_t expected_fault_addr;

static void ftrapv_before(void *fixture)
{
	ARG_UNUSED(fixture);
	expected_fault_addr = 0;
	ztest_set_fault_valid(true);
}

ZTEST_SUITE(ftrapv, NULL, NULL, ftrapv_before, NULL, NULL);

void ztest_post_fatal_error_hook(unsigned int reason,
				 const struct arch_esf *pEsf)
{
	zassert_equal(reason, K_ERR_CPU_EXCEPTION);
	zassert_not_equal(expected_fault_addr, 0,
			  "Expected fault address not set");

	ztest_set_fault_valid(false);

	/* Estimated end of a function. */
	uintptr_t fn_end = expected_fault_addr + 0x40;
#if defined(CONFIG_ARM)
	uintptr_t pc = pEsf->basic.pc;
#elif defined(CONFIG_RISCV)
	uintptr_t pc = pEsf->mepc;
#else
	uintptr_t pc = 0;
	zassert_unreachable("Test not supported on this architecture");
#endif
	/* Make sure Program Counter is stored correctly and points at a
	 * function that causes a crash. */
	zassert_true(pc >= expected_fault_addr && (pc <= fn_end),
		     "PC 0x%" PRIxPTR " not in range [0x%" PRIxPTR
		     ", 0x%" PRIxPTR "]",
		     pc, expected_fault_addr, fn_end);
}

/*
 * trapping addition: __addvsi3.
 */
static void __noinline ftrapv_addition(void)
{
	int32_t test_overflow = INT32_MAX;
	int32_t ret;

	LOG_INF("Testing signed integer addition overflow");
	cflush();
	ret = test_overflow + 1;

	zassert_unreachable();
}

ZTEST(ftrapv, test_ftrapv_addition)
{
	expected_fault_addr = (uintptr_t)ftrapv_addition;
	ftrapv_addition();
}

/*
 * trapping subtraction: __subvsi3.
 */
static void __noinline ftrapv_subtraction(void)
{
	int32_t test_overflow = INT32_MIN;
	int32_t ret;

	LOG_INF("Testing signed integer subtraction overflow");
	cflush();
	ret = test_overflow - 1;

	zassert_unreachable();
}

ZTEST(ftrapv, test_ftrapv_subtraction)
{
	expected_fault_addr = (uintptr_t)ftrapv_subtraction;
	ftrapv_subtraction();
}

/*
 * trapping multiplication: __mulvsi3.
 */
static void __noinline ftrapv_multiplication(void)
{
	int32_t test_overflow = INT32_MAX;
	int32_t ret;

	LOG_INF("Testing signed integer multiplication overflow");
	cflush();
	ret = test_overflow * 2;

	zassert_unreachable();
}

ZTEST(ftrapv, test_ftrapv_multiplication)
{
	expected_fault_addr = (uintptr_t)ftrapv_multiplication;
	ftrapv_multiplication();
}

/*
 * trapping negation: __negvsi2.
 */
static void __noinline ftrapv_negation(void)
{
	int32_t test_overflow = INT32_MIN;
	int32_t ret;

	LOG_INF("Testing signed integer negation overflow");
	cflush();
	ret = -test_overflow;

	zassert_unreachable();
}

ZTEST(ftrapv, test_ftrapv_negation)
{
	expected_fault_addr = (uintptr_t)ftrapv_negation;
	ftrapv_negation();
}

/*
 * trapping absolute value: __absvsi2.
 */
static void __noinline ftrapv_abs(void)
{
	int32_t test_overflow = INT32_MIN;
	int32_t ret;

	LOG_INF("Testing signed integer absolute value overflow\n");
	cflush();

	ret = abs(test_overflow);

	zassert_unreachable();
}

ZTEST(ftrapv, test_ftrapv_abs)
{
	expected_fault_addr = (uintptr_t)ftrapv_abs;
	ftrapv_abs();
}
