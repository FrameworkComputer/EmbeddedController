/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/** @brief Test functions defined in timer.h, like crec_usleep().
 *
 * This test only validates the functionality of code in timer.h and is not
 * expected to accurately measure/check the timing.
 */

#include "common.h"
#include "timer.h"

#include <zephyr/ztest.h>

ZTEST_SUITE(timer, NULL, NULL, NULL, NULL, NULL);

ZTEST(timer, test_usleep)
{
	const int expected_duration = 12345;

	uint64_t start_time = sys_clock_cycle_get_64();
	crec_usleep(expected_duration);
	uint64_t sleep_duration =
		((sys_clock_cycle_get_64() - start_time) * USEC_PER_SEC) /
		sys_clock_hw_cycles_per_sec();
	/* The sleep duration is adjusted to the system tick boundaries. */
	/* The maximum error threshold is two ticks. */
	int error_threshold =
		(USEC_PER_SEC / CONFIG_SYS_CLOCK_TICKS_PER_SEC) * 2;

	if (IS_ENABLED(CONFIG_BOARD_HELIPILOT)) {
		/* TODO(b/309557100): Adjust the threshold for Helipilot as it
		 * is done for CrosEC.
		 */
		zassert_unreachable();
	}

	zassert_true(sleep_duration >= expected_duration);
	zassert_true((sleep_duration - expected_duration) < error_threshold);
}

/* When timestamp_expired is called with NULL for the second parameter,
 * get_time() should be used for the "now" value.
 */
ZTEST(timer, test_timestamp_expired)
{
	/* Set an arbitrary time for "now", all times will be relative to now */
	timestamp_t now = { .val = 2 * HOUR };
	timestamp_t deadline;

	/* set the deadline in the past, verify expired*/
	deadline.val = now.val - 1;
	zassert_true(timestamp_expired(deadline, &now));

	/* set the deadline in the now, verify expired*/
	deadline.val = now.val;
	zassert_true(timestamp_expired(deadline, &now));

	/* set the deadline in the future, verify not expired*/
	deadline.val = now.val + 1;
	zassert_false(timestamp_expired(deadline, &now));
}

/* When timestamp_expired is called with NULL for the second parameter,
 * get_time() should be used for the "now" value.
 */
ZTEST(timer, test_timestamp_expired_null)
{
	timestamp_t deadline;

	/* set the deadline in the past, verify expired */
	deadline.val = get_time().val - 1;
	zassert_true(timestamp_expired(deadline, NULL));

	/* set the deadline to far enough in the future that it will not expire,
	 * verify not expired */
	deadline.val = get_time().val + SECOND;
	zassert_false(timestamp_expired(deadline, NULL));
}
