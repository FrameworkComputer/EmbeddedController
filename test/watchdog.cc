/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "hwtimer.h"
#include "panic.h"
#include "system.h"
#include "task.h"
#include "test_util.h"

#include <cassert>

test_static int test_watchdog()
{
	constexpr uint32_t watchdog_period_us =
		CONFIG_WATCHDOG_PERIOD_MS * MSEC;

	/* Busy wait slightly longer than watchdog period to account for
	 * inaccuracies in busy waiting. */
	uint32_t us_to_wait = watchdog_period_us * 1.1;

	/*
	 * TODO(b/390021325): It appears that the watchdog timer is being set
	 * to longer than CONFIG_WATCHDOG_PERIOD_MS on bloonchipper/dartmonkey.
	 */
	if (IS_ENABLED(BOARD_BLOONCHIPPER) || IS_ENABLED(BOARD_DARTMONKEY)) {
		us_to_wait = watchdog_period_us * 3;
	}

	ccprints("Starting busy loop.");

	udelay(us_to_wait);

	/* Watchdog should fire before reaching this. */
	assert(false);

	return EC_ERROR_UNKNOWN;
}

test_static int test_panic_data()
{
	constexpr uint32_t expected_reason = PANIC_SW_WATCHDOG;
	/* Note: The task_id can be found with the "taskinfo" command. */
	constexpr uint8_t expected_task_id = 5;

	/*
	 * The watchdog should fire while udelay is running. We don't know the
	 * exact end of udelay, but it's short so we just choose a small value
	 * for the end.
	 *
	 * Note that this check depends on build optimizations and inlining,
	 * which is why we have a special case for helipilot. The helipilot
	 * build does not inline the __hw_clock_source_read() function called by
	 * udelay().
	 */
	uint32_t udelay_start_addr = reinterpret_cast<uint32_t>(udelay);
	if (IS_ENABLED(BASEBOARD_HELIPILOT)) {
		udelay_start_addr =
			reinterpret_cast<uint32_t>(__hw_clock_source_read);
	}
	uint32_t udelay_end_addr = udelay_start_addr + 0x40;

	uint32_t reason = 0;
	uint32_t info = 0;
	uint8_t exception = UINT8_MAX;

	panic_get_reason(&reason, &info, &exception);

	TEST_EQ(reason, expected_reason, "%08x");
	TEST_GT(info, udelay_start_addr, "%d");
	TEST_LT(info, udelay_end_addr, "%d");
	TEST_EQ(exception, expected_task_id, "%d");

	return EC_SUCCESS;
}

test_static void run_test_step1()
{
	test_set_next_step(TEST_STATE_STEP_2);
	RUN_TEST(test_watchdog);
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
