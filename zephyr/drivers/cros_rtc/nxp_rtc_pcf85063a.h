/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_RTC_PCF85063A_H
#define __CROS_EC_RTC_PCF85063A_H

#define PCF85063A_REG_NUM       18
#define SOFT_RESET              0x58
#define CONTROL_1_DEFAULT_VALUE 0
#define OS_BIT                  0x80
#define DISABLE_ALARM           0x80
#define ENABLE_ALARM_INTERRUPT	0x80
#define RTC_STOP_CLOCKS		0x20
#define RTC_START_CLOCKS	0x00

#define NUM_TIMER_REGS       7
#define NUM_ALARM_REGS       4

#define REG_CONTROL_1        0x00
#define REG_CONTROL_2        0x01
#define REG_OFFSET           0x02
#define REG_RAM_BYTE         0x03
#define REG_SECONDS          0x04
#define REG_MINUTES          0x05
#define REG_HOURS            0x06
#define REG_DAYS             0x07
#define REG_WEEKDAYS         0x08
#define REG_MONTHS           0x09
#define REG_YEARS            0x0a
#define REG_SECOND_ALARM     0x0b
#define REG_MINUTE_ALARM     0x0c
#define REG_HOUR_ALARM       0x0d
#define REG_DAY_ALARM        0x0e
#define REG_WEEKDAY_ALARM    0x0f
#define REG_TIMER_VALUE      0x10
#define REG_TIMER_MODE       0x11

/* Macros for indexing time_reg buffer */
#define SECONDS           0
#define MINUTES           1
#define HOURS             2
#define DAYS              3
#define WEEKDAYS          4
#define MONTHS            5
#define YEARS             6

enum bcd_mask {
	SECONDS_MASK = 0x70,
	MINUTES_MASK = 0x70,
	HOURS24_MASK = 0x30,
	DAYS_MASK    = 0x30,
	MONTHS_MASK  = 0x10,
	YEARS_MASK   = 0xf0
};

#endif /* __CROS_EC_RTC_PCF85063A_H */
