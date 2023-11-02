/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Clocks and power management settings */

#ifndef __CROS_EC_CLOCK_L4_H
#define __CROS_EC_CLOCK_L4_H

#include "hwtimer.h"
#include "registers.h"
#include "system.h"
#include "timer.h"

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

struct rtc_time_reg {
	uint32_t rtc_ssr; /* subseconds */
	uint32_t rtc_tr; /* hours, minutes, seconds */
	uint32_t rtc_dr; /* years, months, dates, week days */
};

/* Save the RTC alarm wake time */
struct wake_time {
	timestamp_t ts;
	uint32_t rtc_alrmar; /* the value of register STM32_RTC_ALRMAR */
};

/* Convert between RTC regs in BCD and seconds */
uint32_t rtc_to_sec(const struct rtc_time_reg *rtc);

/* Convert between seconds and RTC regs */
void sec_to_rtc(uint32_t sec, struct rtc_time_reg *rtc);

/* Calculate microseconds from rtc sub-second register. */
uint32_t rtcss_to_us(uint32_t rtcss);

/* Calculate rtc sub-second register value from microseconds. */
uint32_t us_to_rtcss(uint32_t us);

/* Return sub-10-sec time diff between two rtc readings */
uint32_t get_rtc_diff(const struct rtc_time_reg *rtc0,
		      const struct rtc_time_reg *rtc1);

/* Read RTC values */
void rtc_read(struct rtc_time_reg *rtc);

/* Set RTC value */
void rtc_set(uint32_t sec);

/* Set RTC wakeup, save alarm wakeup time when save_alarm != 0 */
void set_rtc_alarm(uint32_t delay_s, uint32_t delay_us,
		   struct rtc_time_reg *rtc, uint8_t save_alarm);

/* Clear RTC wakeup */
void reset_rtc_alarm(struct rtc_time_reg *rtc);

/*
 * Return the remaining seconds before the RTC alarm goes off.
 * Sub-seconds are ignored. Returns 0 if alarm is not set.
 */
uint32_t get_rtc_alarm(void);

/* RTC init */
void rtc_init(void);

/* Init high speed clock config */
void config_hispeed_clock(void);

/* Get timer clock frequency (for STM32 only) */
int clock_get_timer_freq(void);

/*
 * Return 1 if host_wake_time is nonzero and the saved host_wake_time
 * is expired at a given time, ts.
 */
bool is_host_wake_alarm_expired(timestamp_t ts);

/* Set RTC wakeup based on the value saved in host_wake_time */
void restore_host_wake_alarm(void);

#ifdef CONFIG_LOW_POWER_IDLE
void low_power_init(void);
#endif

#endif /* __CROS_EC_CLOCK_L4_H */
