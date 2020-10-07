/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "clock-f.h"
#include "test_util.h"

static volatile uint32_t rtc_fired;
static struct rtc_time_reg rtc_irq;
static const int rtc_delay_ms = 500;

/* Override default RTC interrupt handler */
void __rtc_alarm_irq(void)
{
	deprecated_atomic_add(&rtc_fired, 1);
	reset_rtc_alarm(&rtc_irq);
}

test_static int test_rtc_alarm(void)
{
	struct rtc_time_reg rtc;
	uint32_t rtc_diff_us;
	uint32_t rtc_diff_ms;
	const int delay_us = rtc_delay_ms * MSEC;

	set_rtc_alarm(0, delay_us, &rtc, 0);

	msleep(2 * rtc_delay_ms);

	/* Make sure the interrupt fired exactly once. */
	TEST_EQ(1, deprecated_atomic_read_clear(&rtc_fired), "%d");

	rtc_diff_us = get_rtc_diff(&rtc, &rtc_irq);

	ccprintf("rtc_diff_us: %d\n", rtc_diff_us);

	/* Assume we'll always fire within 1 ms. May need to be adjusted if
	 * this doesn't hold.
	 */
	rtc_diff_ms = rtc_diff_us / MSEC;
	TEST_EQ(rtc_diff_ms, rtc_delay_ms, "%d");

	return EC_SUCCESS;
}

void run_test(int argc, char **argv)
{
	RUN_TEST(test_rtc_alarm);

	test_print_result();
}
