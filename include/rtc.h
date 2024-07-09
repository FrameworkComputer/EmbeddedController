/* Copyright 2017 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* RTC cross-platform functions */

#ifndef __CROS_EC_RTC_H
#define __CROS_EC_RTC_H

#include "common.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SECS_PER_MINUTE 60
#define SECS_PER_HOUR (60 * SECS_PER_MINUTE)
#define SECS_PER_DAY (24 * SECS_PER_HOUR)
#define SECS_PER_WEEK (7 * SECS_PER_DAY)
#define SECS_PER_YEAR (365 * SECS_PER_DAY)
/* The seconds elapsed from 01-01-1970 to 01-01-2000 */
#define SECS_TILL_YEAR_2K (946684800)
#define IS_LEAP_YEAR(x) \
	(((x) % 4 == 0) && (((x) % 100 != 0) || ((x) % 400 == 0)))

struct calendar_date {
	/* The number of years since A.D. 2000, i.e. year = 17 for y2017 */
	uint8_t year;
	/* 1-based indexing, i.e. valid values range from 1 to 12 */
	uint8_t month;
	/* 1-based indexing, i.e. valid values range from 1 to 31 */
	uint8_t day;
};

/**
 * Convert calendar date to seconds elapsed since epoch time.
 *
 * @param time  The calendar date (years, months, and days).
 * @return the seconds elapsed since epoch time (01-01-1970 00:00:00).
 */
uint32_t date_to_sec(struct calendar_date time);

/**
 * Convert seconds elapsed since epoch time to calendar date
 *
 * @param sec  The seconds elapsed since epoch time (01-01-1970 00:00:00).
 * @return the calendar date (years, months, and days).
 */
struct calendar_date sec_to_date(uint32_t sec);

#ifdef __cplusplus
}
#endif

#endif /* __CROS_EC_RTC_H */
