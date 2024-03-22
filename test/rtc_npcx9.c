/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "atomic.h"
#include "atomic_bit.h"
#include "clock_chip.h"
#include "system.h"
#include "test_util.h"
#include "timer.h"

/*
 * b/328779928: If you increase this value to 2, the test fails when run with
 * run_device_tests.py, but passes when run on the console manually.
 */
static const int rtc_delay_seconds = 1;
static atomic_t interrupt_counter;
static atomic_t rtc_fired;

void rtc_interrupt_handler(void)
{
	atomic_add(&interrupt_counter, 1);
}

test_static int test_rtc_alarm_fired(void)
{
	atomic_clear(&interrupt_counter);
	system_set_rtc_alarm(rtc_delay_seconds, 0);

	sleep(2 * rtc_delay_seconds);

	rtc_fired = atomic_get(&interrupt_counter);

	TEST_EQ(1, rtc_fired, "%d");
	TEST_EQ(0, system_get_rtc_alarm(), "%d");

	return EC_SUCCESS;
}

test_static int test_rtc_alarm_not_fired(void)
{
	atomic_clear(&interrupt_counter);
	system_set_rtc_alarm(rtc_delay_seconds, 0);

	sleep(0.5 * rtc_delay_seconds);

	rtc_fired = atomic_get(&interrupt_counter);

	TEST_EQ(0, rtc_fired, "%d");
	TEST_EQ(1, system_get_rtc_alarm(), "%d");

	return EC_SUCCESS;
}

static const int rtc_alarm_iterations = 3;

test_static int test_rtc_series_alarm_fired(void)
{
	atomic_clear(&interrupt_counter);

	for (int i = 0; i < rtc_alarm_iterations; ++i) {
		system_set_rtc_alarm(rtc_delay_seconds, 0);
		sleep(2 * rtc_delay_seconds);
		rtc_fired = atomic_get(&interrupt_counter);
		TEST_EQ(i + 1, rtc_fired, "%d");
		TEST_EQ(0, system_get_rtc_alarm(), "%d");
	}

	return EC_SUCCESS;
}

void run_test(int argc, const char **argv)
{
	test_reset();

	RUN_TEST(test_rtc_alarm_fired);
	RUN_TEST(test_rtc_alarm_not_fired);
	RUN_TEST(test_rtc_series_alarm_fired);

	test_print_result();
}
