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

	TEST_NEAR(expected_duration, sleep_duration, 100, "%d");

	return EC_SUCCESS;
}

void run_test(int argc, const char **argv)
{
	test_reset();
	watchdog_reload();

	RUN_TEST(test_usleep);

	test_print_result();
}
