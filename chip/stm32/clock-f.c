/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Clocks and power management settings */

#include "chipset.h"
#include "clock.h"
#include "clock-f.h"
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

/* Console output macros */
#define CPUTS(outstr) cputs(CC_CLOCK, outstr)
#define CPRINTS(format, args...) cprints(CC_CLOCK, format, ## args)


/* Convert between RTC regs in BCD and seconds */
uint32_t rtc_to_sec(uint32_t rtc)
{
	uint32_t sec;

	/* convert the hours field */
	sec = (((rtc & 0x300000) >> 20) * 10 + ((rtc & 0xf0000) >> 16)) * 3600;
	/* convert the minutes field */
	sec += (((rtc & 0x7000) >> 12) * 10 + ((rtc & 0xf00) >> 8)) * 60;
	/* convert the seconds field */
	sec += ((rtc & 0x70) >> 4) * 10 + (rtc & 0xf);

	return sec;
}
uint32_t sec_to_rtc(uint32_t sec)
{
	uint32_t rtc;

	/* convert the hours field */
	rtc = ((sec / 36000) << 20) | (((sec / 3600) % 10) << 16);
	/* convert the minutes field */
	rtc |= (((sec % 3600) / 600) << 12) | (((sec % 600) / 60) << 8);
	/* convert the seconds field */
	rtc |= (((sec % 60) / 10) << 4) | (sec % 10);

	return rtc;
}

/* Return time diff between two rtc readings */
int32_t get_rtc_diff(uint32_t rtc0, uint32_t rtc0ss,
		     uint32_t rtc1, uint32_t rtc1ss)
{
	int32_t diff;

	/* Note: this only looks at the diff mod 10 seconds */
	diff =  ((rtc1 & 0xf) * SECOND +
		 rtcss_to_us(rtc1ss)) -
		((rtc0 & 0xf) * SECOND +
		 rtcss_to_us(rtc0ss));

	return (diff < 0) ? (diff + 10*SECOND) : diff;
}

void rtc_read(uint32_t *rtc, uint32_t *rtcss)
{
	/* Read current time synchronously */
	do {
		*rtc = STM32_RTC_TR;
		/*
		 * RTC_SSR must be read twice with identical values because
		 * glitches may occur for reads close to the RTCCLK edge.
		 */
		do {
			*rtcss = STM32_RTC_SSR;
		} while (*rtcss != STM32_RTC_SSR);
	} while (*rtc != STM32_RTC_TR);
}

void set_rtc_alarm(uint32_t delay_s, uint32_t delay_us,
		      uint32_t *rtc, uint32_t *rtcss)
{
	uint32_t alarm_sec, alarm_us;

	/* Alarm must be within 1 day (86400 seconds) */
	ASSERT((delay_s + delay_us / SECOND) < 86400);

	rtc_unlock_regs();

	/* Make sure alarm is disabled */
	STM32_RTC_CR &= ~STM32_RTC_CR_ALRAE;
	while (!(STM32_RTC_ISR & STM32_RTC_ISR_ALRAWF))
		;
	STM32_RTC_ISR &= ~STM32_RTC_ISR_ALRAF;

	rtc_read(rtc, rtcss);

	/* Calculate alarm time */
	alarm_sec = rtc_to_sec(*rtc) + delay_s;
	alarm_us = rtcss_to_us(*rtcss) + delay_us;
	alarm_sec = alarm_sec + alarm_us / SECOND;
	alarm_us = alarm_us % 1000000;
	/*
	 * If seconds is greater than 1 day, subtract by 1 day to deal with
	 * 24-hour rollover.
	 */
	if (alarm_sec >= 86400)
		alarm_sec -= 86400;

	/* Set alarm time */
	STM32_RTC_ALRMAR = sec_to_rtc(alarm_sec);
	STM32_RTC_ALRMASSR = us_to_rtcss(alarm_us);
	/* Check for match on hours, minutes, seconds, and subsecond */
	STM32_RTC_ALRMAR |= 0xc0000000;
	STM32_RTC_ALRMASSR |= 0x0f000000;

	/* Enable alarm and alarm interrupt */
	STM32_EXTI_PR = EXTI_RTC_ALR_EVENT;
	STM32_EXTI_IMR |= EXTI_RTC_ALR_EVENT;
	STM32_RTC_CR |= STM32_RTC_CR_ALRAE;

	rtc_lock_regs();
}

void reset_rtc_alarm(uint32_t *rtc, uint32_t *rtcss)
{
	rtc_unlock_regs();

	/* Disable alarm */
	STM32_RTC_CR &= ~STM32_RTC_CR_ALRAE;
	STM32_RTC_ISR &= ~STM32_RTC_ISR_ALRAF;

	/* Disable RTC alarm interrupt */
	STM32_EXTI_IMR &= ~EXTI_RTC_ALR_EVENT;
	STM32_EXTI_PR = EXTI_RTC_ALR_EVENT;

	/* Read current time */
	rtc_read(rtc, rtcss);

	rtc_lock_regs();
}

void __rtc_alarm_irq(void)
{
	uint32_t rtc, rtcss;

	reset_rtc_alarm(&rtc, &rtcss);
}
DECLARE_IRQ(STM32_IRQ_RTC_ALARM, __rtc_alarm_irq, 1);

void clock_init(void)
{
	/*
	 * The initial state :
	 *  SYSCLK from HSI (=8MHz), no divider on AHB, APB1, APB2
	 *  PLL unlocked, RTC enabled on LSE
	 */

	/*
	 * put 1 Wait-State for flash access to ensure proper reads at 48Mhz
	 * and enable prefetch buffer.
	 */
	/* Enable data and instruction cache. */
	STM32_FLASH_ACR = STM32_FLASH_ACR_LATENCY | STM32_FLASH_ACR_PRFTEN;

	config_hispeed_clock();

	rtc_init();
}

/*****************************************************************************/
/* Console commands */

#ifdef CONFIG_CMD_RTC_ALARM
static int command_rtc_alarm_test(int argc, char **argv)
{
	int s = 1, us = 0;
	uint32_t rtc, rtcss;
	char *e;

	ccprintf("Setting RTC alarm\n");

	if (argc > 1) {
		s = strtoi(argv[1], &e, 10);
		if (*e)
			return EC_ERROR_PARAM1;

	}
	if (argc > 2) {
		us = strtoi(argv[2], &e, 10);
		if (*e)
			return EC_ERROR_PARAM2;

	}

	set_rtc_alarm(s, us, &rtc, &rtcss);
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(rtc_alarm, command_rtc_alarm_test,
			"[seconds [microseconds]]",
			"Test alarm");
#endif /* CONFIG_CMD_RTC_ALARM */

