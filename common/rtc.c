/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* RTC cross-platform code for Chrome EC */
/* TODO(chromium:733844): Move this conversion to kernel rtc-cros-ec driver */

#include "rtc.h"

static uint16_t days_since_year_start[12] = {
0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};

/* Conversion between calendar date and seconds eclapsed since 1970-01-01 */
uint32_t date_to_sec(struct calendar_date time)
{
	int i;
	uint32_t sec;

	sec = time.year * SECS_PER_YEAR;
	for (i = 0; i < time.year; i++) {
		if (IS_LEAP_YEAR(i))
			sec += SECS_PER_DAY;
	}

	sec += (days_since_year_start[time.month - 1] +
		(IS_LEAP_YEAR(time.year) && time.month > 2) +
		(time.day - 1)) * SECS_PER_DAY;

	/* add the accumulated time in seconds from 1970 to 2000 */
	return sec + SECS_TILL_YEAR_2K;
}

struct calendar_date sec_to_date(uint32_t sec)
{
	struct calendar_date time;
	int day_tmp; /* for intermediate calculation */
	int i;

	/* RTC time must be after year 2000. */
	sec = (sec > SECS_TILL_YEAR_2K) ? (sec - SECS_TILL_YEAR_2K) : 0;

	day_tmp = sec / SECS_PER_DAY;
	time.year = day_tmp / 365;
	day_tmp %= 365;
	for (i = 0; i < time.year; i++) {
		if (IS_LEAP_YEAR(i))
			day_tmp -= 1;
	}
	day_tmp++;
	if (day_tmp <= 0) {
		time.year -= 1;
		day_tmp += IS_LEAP_YEAR(time.year) ? 366 : 365;
	}
	for (i = 1; i < 12; i++) {
		if (days_since_year_start[i] +
		    (IS_LEAP_YEAR(time.year) && (i >= 2)) >= day_tmp)
			break;
	}
	time.month = i;

	day_tmp -= days_since_year_start[time.month - 1] +
		   (IS_LEAP_YEAR(time.year) && (time.month > 2));
	time.day = day_tmp;

	return time;
}
