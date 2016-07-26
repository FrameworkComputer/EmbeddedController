/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Clocks and power management settings */

#ifndef __CROS_EC_CLOCK_F_H
#define __CROS_EC_CLOCK_F_H

#include "chipset.h"
#include "clock.h"
#include "common.h"
#include "console.h"
#include "cpu.h"
#include "hooks.h"
#include "hwtimer.h"
#include "registers.h"
#include "system.h"
#include "task.h"
#include "timer.h"
#include "util.h"

/* Lock and unlock RTC write access */
static inline void rtc_lock_regs(void)
{
	STM32_RTC_WPR = 0xff;
}
static inline void rtc_unlock_regs(void)
{
	STM32_RTC_WPR = 0xca;
	STM32_RTC_WPR = 0x53;
}

/* Convert between RTC regs in BCD and seconds */
uint32_t rtc_to_sec(uint32_t rtc);

/* Convert between seconds and RTC regs */
uint32_t sec_to_rtc(uint32_t sec);

/* Calculate microseconds from rtc clocks. */
int32_t rtcss_to_us(uint32_t rtcss);

/* Calculate rtc clocks from microseconds. */
uint32_t us_to_rtcss(int32_t us);

/* Return time diff between two rtc readings */
int32_t get_rtc_diff(uint32_t rtc0, uint32_t rtc0ss,
		     uint32_t rtc1, uint32_t rtc1ss);

/* Read RTC values */
void rtc_read(uint32_t *rtc, uint32_t *rtcss);

/* Set RTC wakeup */
void set_rtc_alarm(uint32_t delay_s, uint32_t delay_us,
		      uint32_t *rtc, uint32_t *rtcss);

/* Clear RTC wakeup */
void reset_rtc_alarm(uint32_t *rtc, uint32_t *rtcss);

/* RTC init */
void rtc_init(void);

/* Init clock blocks and finctionality */
void clock_init(void);

/* Init high speed clock config */
void config_hispeed_clock(void);

#endif  /* __CROS_EC_CLOCK_F_H */
