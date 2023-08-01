/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "panic.h"
#include "test/drivers/test_mocks.h"
#include "test/drivers/test_state.h"

#include <zephyr/ztest.h>

ZTEST_SUITE(panic_output, drivers_predicate_post_main, NULL, NULL, NULL, NULL);

ZTEST(panic_output, test_panic_printf)
{
	panic_printf("test output string from %s\n", __func__);
}

ZTEST(panic_output, test_panic_puts)
{
	panic_puts("test output string\n");
}

ZTEST(panic_output, test_panic_sw_reason_is_valid)
{
	zassert_false(panic_sw_reason_is_valid(PANIC_SW_BASE - 1), NULL);
	/* PANIC_SW_DIV_ZERO */
	zassert_true(panic_sw_reason_is_valid(PANIC_SW_BASE), NULL);
	/* PANIC_SW_STACK_OVERFLOW */
	zassert_true(panic_sw_reason_is_valid(PANIC_SW_BASE + 1), NULL);
	/* PANIC_SW_PD_CRASH */
	zassert_true(panic_sw_reason_is_valid(PANIC_SW_BASE + 2), NULL);
	/* PANIC_SW_ASSERT */
	zassert_true(panic_sw_reason_is_valid(PANIC_SW_BASE + 3), NULL);
	/* PANIC_SW_WATCHDOG */
	zassert_true(panic_sw_reason_is_valid(PANIC_SW_BASE + 4), NULL);
	/* PANIC_SW_RNG */
	zassert_true(panic_sw_reason_is_valid(PANIC_SW_BASE + 5), NULL);
	/* PANIC_SW_PMIC_FAULT */
	zassert_true(panic_sw_reason_is_valid(PANIC_SW_BASE + 6), NULL);
	/* PANIC_SW_EXIT */
	zassert_true(panic_sw_reason_is_valid(PANIC_SW_BASE + 7), NULL);
	/* PANIC_SW_WATCHDOG_WARN */
	zassert_true(panic_sw_reason_is_valid(PANIC_SW_BASE + 8), NULL);
	zassert_false(panic_sw_reason_is_valid(PANIC_SW_BASE + 9), NULL);
}

ZTEST(panic_output, test_panic)
{
	panic(__func__);
	zassert_equal(1, system_reset_fake.call_count,
		      "Expected system_reset() to be called once, but was "
		      "called %d times",
		      system_reset_fake.call_count);
	zassert_equal(0, system_reset_fake.arg0_val,
		      "Expected system_reset() to be called with flags=0, but "
		      "got flags=%d",
		      system_reset_fake.arg0_val);
}

ZTEST(panic_output, test_panic_assert_fail)
{
	int line_num = __LINE__;

	panic_assert_fail("Test panic message", __func__, __FILE__, line_num);
	zassert_equal(1, software_panic_fake.call_count,
		      "Expected sofware_panic() to be called once, but was "
		      "called %d times",
		      software_panic_fake.call_count);
	zassert_equal(PANIC_SW_ASSERT, software_panic_fake.arg0_val,
		      "Expected software_panic() to be called with "
		      "reason=%d (PANIC_SW_ASSERT) but got %d",
		      PANIC_SW_ASSERT, software_panic_fake.arg0_val);
	zassert_equal(line_num, software_panic_fake.arg1_val,
		      "Expected software_panic() to be called with "
		      "line=%d but got %d",
		      line_num, software_panic_fake.arg1_val);
}
