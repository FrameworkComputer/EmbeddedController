/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "panic.h"
#include "test/drivers/test_mocks.h"
#include "test/drivers/test_state.h"

#include <zephyr/ztest.h>

ZTEST_SUITE(panic_output, drivers_predicate_post_main, NULL, NULL, NULL, NULL);

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
	/* PANIC_SW_WATCHDOG_HARD */
	zassert_true(panic_sw_reason_is_valid(PANIC_SW_BASE + 9), NULL);
	zassert_false(panic_sw_reason_is_valid(PANIC_SW_BASE + 10), NULL);
}
