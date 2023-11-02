/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "clock_chip.h"
#include "test_util.h"

static uint32_t rtc_fired;
static struct rtc_time_reg rtc_irq;
static const int rtc_delay_ms = 500;

/*
 * We will be testing that the RTC interrupt timestamp occurs
 * within +/- delay_tol_us (tolerance) of the above rtc_delay_ms.
 */
static const int delay_tol_us = MSEC / 2;

/* Override default RTC interrupt handler */
void rtc_alarm_irq(void)
{
	atomic_add(&rtc_fired, 1);
	reset_rtc_alarm(&rtc_irq);
}

test_static int test_rtc_alarm(void)
{
	struct rtc_time_reg rtc;
	uint32_t rtc_diff_us;
	const int delay_us = rtc_delay_ms * MSEC;

	set_rtc_alarm(0, delay_us, &rtc, 0);

	msleep(2 * rtc_delay_ms);

	/* Make sure the interrupt fired exactly once. */
	TEST_EQ(1, atomic_clear(&rtc_fired), "%d");

	rtc_diff_us = get_rtc_diff(&rtc, &rtc_irq);

	ccprintf("Target delay was %dus\n", delay_us);
	ccprintf("Actual delay was %dus\n", rtc_diff_us);
	ccprintf("The delays are expected to be within +/- %dus\n", MSEC / 2);

	/* Assume we'll always fire within 500us. May need to be adjusted if
	 * this doesn't hold.
	 *
	 * delay_us-delay_tol_us < rtc_diff_us < delay_us+delay_tol_us
	 */
	TEST_LT(delay_us - delay_tol_us, rtc_diff_us, "%dus");
	TEST_LT(rtc_diff_us, delay_us + delay_tol_us, "%dus");

	return EC_SUCCESS;
}

static const int rtc_match_delay_iterations = 5000;

test_static int test_rtc_match_delay(void)
{
	struct rtc_time_reg rtc;
	int i;

	atomic_clear(&rtc_fired);
	for (i = 0; i < rtc_match_delay_iterations; i++) {
		set_rtc_alarm(0, SET_RTC_MATCH_DELAY, &rtc, 0);
		usleep(2 * SET_RTC_MATCH_DELAY);
	}

	ccprintf("Expected number of RTC alarm interrupts %d\n",
		 rtc_match_delay_iterations);
	ccprintf("Actual number of RTC alarm interrupts %d\n", rtc_fired);

	/* Make sure each set_rtc_alarm() generated the interrupt. */
	TEST_EQ(rtc_match_delay_iterations, atomic_clear(&rtc_fired), "%d");

	return EC_SUCCESS;
}

void run_test(int argc, const char **argv)
{
	test_reset();

	RUN_TEST(test_rtc_alarm);
	RUN_TEST(test_rtc_match_delay);

	test_print_result();
}
