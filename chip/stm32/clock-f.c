/* Copyright 2016 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Clocks and power management settings */

#include "builtin/assert.h"
#include "chipset.h"
#include "clock.h"
#include "clock_chip.h"
#include "common.h"
#include "console.h"
#include "cpu.h"
#include "hooks.h"
#include "host_command.h"
#include "hwtimer.h"
#include "registers.h"
#include "rtc.h"
#include "system.h"
#include "task.h"
#include "timer.h"
#include "util.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_CLOCK, outstr)
#define CPRINTS(format, args...) cprints(CC_CLOCK, format, ##args)

/* Convert decimal to BCD */
static uint8_t u8_to_bcd(uint8_t val)
{
	/* Fast division by 10 (when lacking HW div) */
	uint32_t quot = ((uint32_t)val * 0xCCCD) >> 19;
	uint32_t rem = val - quot * 10;

	return rem | (quot << 4);
}

/* Convert between RTC regs in BCD and seconds */
static uint32_t rtc_tr_to_sec(uint32_t rtc_tr)
{
	uint32_t sec;

	/* convert the hours field */
	sec = (((rtc_tr & 0x300000) >> 20) * 10 + ((rtc_tr & 0xf0000) >> 16)) *
	      3600;
	/* convert the minutes field */
	sec += (((rtc_tr & 0x7000) >> 12) * 10 + ((rtc_tr & 0xf00) >> 8)) * 60;
	/* convert the seconds field */
	sec += ((rtc_tr & 0x70) >> 4) * 10 + (rtc_tr & 0xf);
	return sec;
}

static uint32_t sec_to_rtc_tr(uint32_t sec)
{
	uint32_t rtc_tr;
	uint8_t hour;
	uint8_t min;

	sec %= SECS_PER_DAY;
	/* convert the hours field */
	hour = sec / 3600;
	rtc_tr = u8_to_bcd(hour) << 16;
	/* convert the minutes field */
	sec -= hour * 3600;
	min = sec / 60;
	rtc_tr |= u8_to_bcd(min) << 8;
	/* convert the seconds field */
	sec -= min * 60;
	rtc_tr |= u8_to_bcd(sec);

	return rtc_tr;
}

/* Register setup before RTC alarm is allowed for update */
static void pre_work_set_rtc_alarm(void)
{
	rtc_unlock_regs();

	/* Make sure alarm is disabled */
	STM32_RTC_CR &= ~STM32_RTC_CR_ALRAE;
	while (!(STM32_RTC_ISR & STM32_RTC_ISR_ALRAWF))
		;
	STM32_RTC_ISR &= ~STM32_RTC_ISR_ALRAF;
}

/* Register setup after RTC alarm is updated */
static void post_work_set_rtc_alarm(void)
{
	STM32_EXTI_PR = EXTI_RTC_ALR_EVENT;

	/* Enable alarm and alarm interrupt */
	STM32_EXTI_IMR |= EXTI_RTC_ALR_EVENT;
	STM32_RTC_CR |= STM32_RTC_CR_ALRAE;

	rtc_lock_regs();
}

#ifdef CONFIG_HOSTCMD_RTC
static struct wake_time host_wake_time;

bool is_host_wake_alarm_expired(timestamp_t ts)
{
	return host_wake_time.ts.val &&
	       timestamp_expired(host_wake_time.ts, &ts);
}

void restore_host_wake_alarm(void)
{
	if (!host_wake_time.ts.val)
		return;

	pre_work_set_rtc_alarm();

	/* Set alarm time */
	STM32_RTC_ALRMAR = host_wake_time.rtc_alrmar;

	post_work_set_rtc_alarm();
}

static uint32_t rtc_dr_to_sec(uint32_t rtc_dr)
{
	struct calendar_date time;
	uint32_t sec;

	time.year =
		(((rtc_dr & 0xf00000) >> 20) * 10 + ((rtc_dr & 0xf0000) >> 16));
	time.month = (((rtc_dr & 0x1000) >> 12) * 10 + ((rtc_dr & 0xf00) >> 8));
	time.day = ((rtc_dr & 0x30) >> 4) * 10 + (rtc_dr & 0xf);

	sec = date_to_sec(time);

	return sec;
}

static uint32_t sec_to_rtc_dr(uint32_t sec)
{
	struct calendar_date time;
	uint32_t rtc_dr;

	time = sec_to_date(sec);

	rtc_dr = u8_to_bcd(time.year) << 16;
	rtc_dr |= u8_to_bcd(time.month) << 8;
	rtc_dr |= u8_to_bcd(time.day);

	return rtc_dr;
}
#endif

uint32_t rtc_to_sec(const struct rtc_time_reg *rtc)
{
	uint32_t sec = 0;
#ifdef CONFIG_HOSTCMD_RTC
	sec = rtc_dr_to_sec(rtc->rtc_dr);
#endif
	return sec + (rtcss_to_us(rtc->rtc_ssr) / SECOND) +
	       rtc_tr_to_sec(rtc->rtc_tr);
}

void sec_to_rtc(uint32_t sec, struct rtc_time_reg *rtc)
{
	rtc->rtc_dr = 0;
#ifdef CONFIG_HOSTCMD_RTC
	rtc->rtc_dr = sec_to_rtc_dr(sec);
#endif
	rtc->rtc_tr = sec_to_rtc_tr(sec);
	rtc->rtc_ssr = 0;
}

/* Return sub-10-sec time diff between two rtc readings
 *
 * Note: this function assumes rtc0 was sampled before rtc1.
 * Additionally, this function only looks at the difference mod 10
 * seconds.
 */
uint32_t get_rtc_diff(const struct rtc_time_reg *rtc0,
		      const struct rtc_time_reg *rtc1)
{
	uint32_t rtc0_val, rtc1_val, diff;

	rtc0_val = (rtc0->rtc_tr & 0xF) * SECOND + rtcss_to_us(rtc0->rtc_ssr);
	rtc1_val = (rtc1->rtc_tr & 0xF) * SECOND + rtcss_to_us(rtc1->rtc_ssr);
	diff = rtc1_val;
	if (rtc1_val < rtc0_val) {
		/* rtc_ssr has wrapped, since we assume rtc0 < rtc1, add
		 * 10 seconds to get the correct value
		 */
		diff += 10 * SECOND;
	}
	diff -= rtc0_val;
	return diff;
}

void rtc_read(struct rtc_time_reg *rtc)
{
	/*
	 * Read current time synchronously. Each register must be read
	 * twice with identical values because glitches may occur for reads
	 * close to the RTCCLK edge.
	 */
	do {
		rtc->rtc_dr = STM32_RTC_DR;

		do {
			rtc->rtc_tr = STM32_RTC_TR;

			do {
				rtc->rtc_ssr = STM32_RTC_SSR;
			} while (rtc->rtc_ssr != STM32_RTC_SSR);

		} while (rtc->rtc_tr != STM32_RTC_TR);

	} while (rtc->rtc_dr != STM32_RTC_DR);
}

void set_rtc_alarm(uint32_t delay_s, uint32_t delay_us,
		   struct rtc_time_reg *rtc, uint8_t save_alarm)
{
	uint32_t alarm_sec = 0;
	uint32_t alarm_us = 0;

	if (delay_s == EC_RTC_ALARM_CLEAR && !delay_us) {
		reset_rtc_alarm(rtc);
		return;
	}

	/* Alarm timeout must be within 1 day (86400 seconds) */
	ASSERT((delay_s + delay_us / SECOND) < SECS_PER_DAY);

	pre_work_set_rtc_alarm();
	rtc_read(rtc);

	/* Calculate alarm time */
	alarm_sec = rtc_tr_to_sec(rtc->rtc_tr) + delay_s;

	if (delay_us) {
		alarm_us = rtcss_to_us(rtc->rtc_ssr) + delay_us;
		alarm_sec = alarm_sec + alarm_us / SECOND;
		alarm_us = alarm_us % SECOND;
	}

	/*
	 * If seconds is greater than 1 day, subtract by 1 day to deal with
	 * 24-hour rollover.
	 */
	if (alarm_sec >= SECS_PER_DAY)
		alarm_sec -= SECS_PER_DAY;

	/*
	 * Set alarm time in seconds and check for match on
	 * hours, minutes, and seconds.
	 */
	STM32_RTC_ALRMAR = sec_to_rtc_tr(alarm_sec) | 0xc0000000;

	/*
	 * Set alarm time in subseconds and check for match on subseconds.
	 * If the caller doesn't specify subsecond delay (e.g. host command),
	 * just align the alarm time to second.
	 */
	STM32_RTC_ALRMASSR = delay_us ? (us_to_rtcss(alarm_us) | 0x0f000000) :
					0;

#ifdef CONFIG_HOSTCMD_RTC
	/*
	 * If alarm is set by the host, preserve the wake time timestamp
	 * and alarm registers.
	 */
	if (save_alarm) {
		host_wake_time.ts.val = delay_s * SECOND + get_time().val;
		host_wake_time.rtc_alrmar = STM32_RTC_ALRMAR;
	}
#endif
	post_work_set_rtc_alarm();
}

uint32_t get_rtc_alarm(void)
{
	struct rtc_time_reg now;
	uint32_t now_sec;
	uint32_t alarm_sec;

	if (!(STM32_RTC_CR & STM32_RTC_CR_ALRAE))
		return 0;

	rtc_read(&now);

	now_sec = rtc_tr_to_sec(now.rtc_tr);
	alarm_sec = rtc_tr_to_sec(STM32_RTC_ALRMAR & 0x3fffff);

	return ((alarm_sec < now_sec) ? SECS_PER_DAY : 0) +
	       (alarm_sec - now_sec);
}

void reset_rtc_alarm(struct rtc_time_reg *rtc)
{
	rtc_unlock_regs();

	/* Disable alarm */
	STM32_RTC_CR &= ~STM32_RTC_CR_ALRAE;
	STM32_RTC_ISR &= ~STM32_RTC_ISR_ALRAF;

	/* Disable RTC alarm interrupt */
	STM32_EXTI_IMR &= ~EXTI_RTC_ALR_EVENT;
	STM32_EXTI_PR = EXTI_RTC_ALR_EVENT;

	/* Clear the pending RTC alarm IRQ in NVIC */
	task_clear_pending_irq(STM32_IRQ_RTC_ALARM);

	/* Read current time */
	rtc_read(rtc);

	rtc_lock_regs();
}

#ifdef CONFIG_HOSTCMD_RTC
static void set_rtc_host_event(void)
{
	host_set_single_event(EC_HOST_EVENT_RTC);
}
DECLARE_DEFERRED(set_rtc_host_event);
#endif

test_mockable void rtc_alarm_irq(void)
{
	struct rtc_time_reg rtc;
	reset_rtc_alarm(&rtc);

#ifdef CONFIG_HOSTCMD_RTC
	/* Wake up the host if there is a saved rtc wake alarm. */
	if (host_wake_time.ts.val) {
		host_wake_time.ts.val = 0;
		hook_call_deferred(&set_rtc_host_event_data, 0);
	}
#endif
}

static void __rtc_alarm_irq(void)
{
	rtc_alarm_irq();
}
DECLARE_IRQ(STM32_IRQ_RTC_ALARM, __rtc_alarm_irq, 1);

__attribute__((weak)) int clock_get_timer_freq(void)
{
	return clock_get_freq();
}

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
	STM32_FLASH_ACR = STM32_FLASH_ACR_LATENCY | STM32_FLASH_ACR_PRFTEN;

#ifdef CHIP_FAMILY_STM32F4
	/* Enable data and instruction cache. */
	STM32_FLASH_ACR |= STM32_FLASH_ACR_DCEN | STM32_FLASH_ACR_ICEN;
#endif

	config_hispeed_clock();

	rtc_init();
}

#ifdef CHIP_FAMILY_STM32F4
void reset_flash_cache(void)
{
	/* Disable data and instruction cache. */
	STM32_FLASH_ACR &= ~(STM32_FLASH_ACR_DCEN | STM32_FLASH_ACR_ICEN);

	/* Reset data and instruction cache */
	STM32_FLASH_ACR |= STM32_FLASH_ACR_DCRST | STM32_FLASH_ACR_ICRST;
}
DECLARE_HOOK(HOOK_SYSJUMP, reset_flash_cache, HOOK_PRIO_DEFAULT);
#endif

/*****************************************************************************/
/* Console commands */

void print_system_rtc(enum console_channel ch)
{
	uint32_t sec;
	struct rtc_time_reg rtc;

	rtc_read(&rtc);
	sec = rtc_to_sec(&rtc);

	cprintf(ch, "RTC: 0x%08x (%d.00 s)\n", sec, sec);
}

#ifdef CONFIG_CMD_RTC
static int command_system_rtc(int argc, const char **argv)
{
	char *e;
	uint32_t t;

	if (argc == 3 && !strcasecmp(argv[1], "set")) {
		t = strtoi(argv[2], &e, 0);
		if (*e)
			return EC_ERROR_PARAM2;
		rtc_set(t);
	} else if (argc > 1)
		return EC_ERROR_INVAL;

	print_system_rtc(CC_COMMAND);

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(rtc, command_system_rtc, "[set <seconds>]",
			"Get/set real-time clock");

#ifdef CONFIG_CMD_RTC_ALARM
static int command_rtc_alarm_test(int argc, const char **argv)
{
	int s = 1, us = 0;
	struct rtc_time_reg rtc;
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

	set_rtc_alarm(s, us, &rtc, 0);
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(rtc_alarm, command_rtc_alarm_test,
			"[seconds [microseconds]]", "Test alarm");
#endif /* CONFIG_CMD_RTC_ALARM */
#endif /* CONFIG_CMD_RTC */

/*****************************************************************************/
/* Host commands */

#ifdef CONFIG_HOSTCMD_RTC
static enum ec_status system_rtc_get_value(struct host_cmd_handler_args *args)
{
	struct ec_response_rtc *r = args->response;
	struct rtc_time_reg rtc;

	rtc_read(&rtc);
	r->time = rtc_to_sec(&rtc);
	args->response_size = sizeof(*r);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_RTC_GET_VALUE, system_rtc_get_value,
		     EC_VER_MASK(0));

static enum ec_status system_rtc_set_value(struct host_cmd_handler_args *args)
{
	const struct ec_params_rtc *p = args->params;

	rtc_set(p->time);
	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_RTC_SET_VALUE, system_rtc_set_value,
		     EC_VER_MASK(0));

static enum ec_status system_rtc_set_alarm(struct host_cmd_handler_args *args)
{
	struct rtc_time_reg rtc;
	const struct ec_params_rtc *p = args->params;

	/* Alarm timeout must be within 1 day (86400 seconds) */
	if (p->time >= SECS_PER_DAY)
		return EC_RES_INVALID_PARAM;

	set_rtc_alarm(p->time, 0, &rtc, 1);
	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_RTC_SET_ALARM, system_rtc_set_alarm,
		     EC_VER_MASK(0));

static enum ec_status system_rtc_get_alarm(struct host_cmd_handler_args *args)
{
	struct ec_response_rtc *r = args->response;

	r->time = get_rtc_alarm();
	args->response_size = sizeof(*r);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_RTC_GET_ALARM, system_rtc_get_alarm,
		     EC_VER_MASK(0));

#endif /* CONFIG_HOSTCMD_RTC */
