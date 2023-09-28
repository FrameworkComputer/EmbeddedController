/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/** @brief Test functions defined in timer.h, like usleep().
 *
 * This test only validates the functionality of code in timer.h and is not
 * expected to accurately measure/check the timing.
 */

#include "common.h"
#include "math_util.h"
#include "test_util.h"

extern "C" {
#include "timer.h"
#include "watchdog.h"
}

test_static int test_usleep(void)
{
	constexpr int expected_duration = 12345;

	timestamp_t start_time = get_time();
	usleep(expected_duration);
	int sleep_duration = time_since32(start_time);
	int error_threshold;

	/* Helipilot uses the LFCLK for events which runs at 32768 Hz with an
	 * error of 2%. This gives a 30.5 us resolution and a max error of 246.9
	 * us on 12345 us. This is considerably lower resolution and higher
	 * error than the stm32 boards and may result in higher deltas.
	 */
	if (IS_ENABLED(BOARD_HELIPILOT)) {
		float max_error = expected_duration * 0.02;
		float clock_tick_us = (1.0 / 32768.0) * 1000000.0;

		/* Assume a worst case error of max_error + 1 clock tick */
		error_threshold = static_cast<int>(max_error + clock_tick_us);
	} else {
		error_threshold = 100;
	}

	TEST_NEAR(expected_duration, sleep_duration, error_threshold, "%d");

	return EC_SUCCESS;
}

/* When timestamp_expired is called with NULL for the second parameter,
 * get_time() should be used for the "now" value.
 */
test_static int test_timestamp_expired(void)
{
	/* Set an arbitrary time for "now", all times will be relative to now */
	timestamp_t now = { .val = 2 * HOUR };
	timestamp_t deadline;

	/* set the deadline in the past, verify expired*/
	deadline.val = now.val - 1;
	TEST_ASSERT(timestamp_expired(deadline, &now));

	/* set the deadline in the now, verify expired*/
	deadline.val = now.val;
	TEST_ASSERT(timestamp_expired(deadline, &now));

	/* set the deadline in the future, verify not expired*/
	deadline.val = now.val + 1;
	TEST_ASSERT(!timestamp_expired(deadline, &now));

	return EC_SUCCESS;
}

/* When timestamp_expired is called with NULL for the second parameter,
 * get_time() should be used for the "now" value.
 */
test_static int test_timestamp_expired_null(void)
{
	timestamp_t deadline;

	/* set the deadline in the past, verify expired */
	deadline.val = get_time().val - 1;
	TEST_ASSERT(timestamp_expired(deadline, NULL));

	/* set the deadline to far enough in the future that it will not expire,
	 * verify not expired */
	deadline.val = get_time().val + SECOND;
	TEST_ASSERT(!timestamp_expired(deadline, NULL));

	return EC_SUCCESS;
}

void run_test(int argc, const char **argv)
{
	test_reset();
	watchdog_reload();

	RUN_TEST(test_usleep);
	RUN_TEST(test_timestamp_expired);
	RUN_TEST(test_timestamp_expired_null);

	test_print_result();
}
