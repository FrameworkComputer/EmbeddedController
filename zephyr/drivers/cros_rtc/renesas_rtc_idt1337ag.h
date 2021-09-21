/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_RTC_IDT1337AG_H
#define __CROS_EC_RTC_IDT1337AG_H

/* Setting bit 6 of register 0Ah selects the DAY as alarm source */
#define SELECT_DAYS_ALARM  0x40
#define DISABLE_ALARM      0x80

#define CONTROL_A1IE       BIT(0)
#define CONTROL_A2IE       BIT(1)
#define CONTROL_INTCN      BIT(2)
#define CONTROL_EOSC       BIT(7)

#define STATUS_A1F         BIT(0)
#define STATUS_A2F         BIT(1)
#define STATUS_OSF         BIT(7)

#define NUM_TIMER_REGS       7
#define NUM_ALARM_REGS       4

#define REG_SECONDS        0x00
#define REG_MINUTES        0x01
#define REG_HOURS          0x02
#define REG_DAYS           0x03
#define REG_DATE           0x04
#define REG_MONTHS         0x05
#define REG_YEARS          0x06
#define REG_SECOND_ALARM1  0x07
#define REG_MINUTE_ALARM1  0x08
#define REG_HOUR_ALARM1    0x09
#define REG_DAY_ALARM1     0x0a
#define REG_MINUTE_ALARM2  0x0b
#define REG_HOUR_ALARM2    0x0c
#define REG_DAY_ALARM2     0x0d
#define REG_CONTROL        0x0e
#define REG_STATUS         0x0f

/* Macros for indexing time_reg buffer */
#define SECONDS           0
#define MINUTES           1
#define HOURS             2
#define DAYS              3
#define DATE              4
#define MONTHS            5
#define YEARS             6

enum bcd_mask {
	SECONDS_MASK = 0x70,
	MINUTES_MASK = 0x70,
	HOURS24_MASK = 0x30,
	DAYS_MASK    = 0x00,
	MONTHS_MASK  = 0x10,
	YEARS_MASK   = 0xf0
};

#endif /* __CROS_EC_RTC_IDT1337AG_H */
