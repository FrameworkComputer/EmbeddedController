/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Tests for rtc time conversions
 */

#include "console.h"
#include "common.h"
#include "rtc.h"
#include "test_util.h"
#include "util.h"

/* Known conversion pairs of date and epoch time. */
static struct {
	struct calendar_date time;
	uint32_t sec;
} test_case[] = {
	{{8, 3, 1}, 1204329600},
	{{17, 10, 1}, 1506816000},
};

static int calendar_time_comp(struct calendar_date time_1,
			   struct calendar_date time_2)
{
	return (time_1.year == time_2.year &&
		time_1.month == time_2.month &&
		time_1.day == time_2.day);
}

static int test_time_conversion(void)
{
	struct calendar_date time_1;
	struct calendar_date time_2;
	uint32_t sec;
	int i;

	/* The seconds elapsed from 01-01-1970 to 01-01-2000 */
	sec = SECS_TILL_YEAR_2K;
	time_1.year = 0;
	time_1.month = 1;
	time_1.day = 1;

	/* Test from year 2000 to 2050 */
	for (i = 0; i <= 50; i++) {
		/* Test Jan. 1 */
		time_1.year = i;
		time_1.month = 1;
		time_1.day = 1;

		TEST_ASSERT(date_to_sec(time_1) == sec);
		time_2 = sec_to_date(sec);
		TEST_ASSERT(calendar_time_comp(time_1, time_2));

		/* Test the day boundary between Jan. 1 and Jan. 2 */
		time_2 = sec_to_date(sec + SECS_PER_DAY - 1);
		TEST_ASSERT(calendar_time_comp(time_1, time_2));

		time_1.day = 2;

		TEST_ASSERT(date_to_sec(time_1) == sec + SECS_PER_DAY);
		time_2 = sec_to_date(sec + SECS_PER_DAY);
		TEST_ASSERT(calendar_time_comp(time_1, time_2));

		/*
		 * Test the month boundary and leap year:
		 * Is the 60th day of a year Mar. 1 or Feb. 29?
		 */
		time_2 = sec_to_date(sec + 59 * SECS_PER_DAY);
		if (IS_LEAP_YEAR(i))
			TEST_ASSERT(time_2.month == 2 && time_2.day == 29);
		else
			TEST_ASSERT(time_2.month == 3 && time_2.day == 1);

		/* Test the year boundary on Dec. 31 */
		sec += SECS_PER_YEAR - (IS_LEAP_YEAR(i) ? 0 : SECS_PER_DAY);
		time_1.month = 12;
		time_1.day = 31;

		TEST_ASSERT(date_to_sec(time_1) == sec);
		time_2 = sec_to_date(sec);
		TEST_ASSERT(calendar_time_comp(time_1, time_2));

		sec += SECS_PER_DAY;
		time_2 = sec_to_date(sec - 1);
		TEST_ASSERT(calendar_time_comp(time_1, time_2));
	}

	/* Verify known test cases */
	for (i = 0; i < ARRAY_SIZE(test_case); i++) {
		TEST_ASSERT(date_to_sec(test_case[i].time) == test_case[i].sec);
		time_1 = sec_to_date(test_case[i].sec);
		TEST_ASSERT(calendar_time_comp(time_1, test_case[i].time));
	}

	return EC_SUCCESS;
}

void run_test(int argc, char **argv)
{
	RUN_TEST(test_time_conversion);

	test_print_result();
}
