/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_DRIVER_FINGERPRINT_EGIS_PLATFORM_INC_PLAT_TIME_H_
#define __CROS_EC_DRIVER_FINGERPRINT_EGIS_PLATFORM_INC_PLAT_TIME_H_

unsigned long long plat_get_time(void);
unsigned long plat_get_diff_time(unsigned long long begin);
void plat_wait_time(unsigned long msecs);

void plat_sleep_time(unsigned long timeInMs);

#ifdef EGIS_SPEED_DBG
#include "plat_log.h"
#define TIME_MEASURE_START(name) \
	unsigned long long timeMeasureStart##name = plat_get_time();
#define TIME_MEASURE_STOP(name, x)                                       \
	unsigned long name = plat_get_diff_time(timeMeasureStart##name); \
	egislog_d(x SPEED_TEST_STR, name);
#define TIME_MEASURE_STOP_INFO(name, x)                                  \
	unsigned long name = plat_get_diff_time(timeMeasureStart##name); \
	egislog_i(x SPEED_TEST_STR, name);
#define TIME_MEASURE_STOP_AND_RESTART(name, x)                         \
	{                                                              \
		egislog_d(x SPEED_TEST_STR,                            \
			  plat_get_diff_time(timeMeasureStart##name)); \
		timeMeasureStart##name = plat_get_time();              \
	}
#define TIME_MEASURE_RESET(name) timeMeasureStart##name = plat_get_time();
#else
#define TIME_MEASURE_START(name)
#define TIME_MEASURE_STOP(name, x)
#define TIME_MEASURE_STOP_INFO(name, x)
#define TIME_MEASURE_STOP_AND_RESTART(name, x)
#define TIME_MEASURE_RESET(name)
#endif

#endif /* __CROS_EC_DRIVER_FINGERPRINT_EGIS_PLATFORM_INC_PLAT_TIME_H_ */
