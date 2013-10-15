/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Clocks and power management settings */

#include "atomic.h"
#include "clock.h"
#include "common.h"
#include "console.h"
#include "cpu.h"
#include "hwtimer.h"
#include "registers.h"
#include "system.h"
#include "task.h"
#include "timer.h"
#include "util.h"

/* Allow serial console to wake up the EC from STOP mode */
/* #define CONFIG_FORCE_CONSOLE_RESUME */

/*
 * minimum delay to enter stop mode
 * STOP mode wakeup time with regulator in low power mode is 5 us.
 * PLL locking time is 200us.
 */
#define STOP_MODE_LATENCY 300 /* us */

/*
 * RTC clock frequency (connected to LSI clock)
 *
 * TODO: crosbug.com/p/12281 calibrate LSI frequency
 */
#define RTC_FREQ 40000 /* Hz */
#define US_PER_RTC_TICK (1000000 / RTC_FREQ)

static void wait_rtc_ready(void)
{
	/* wait for Registers Synchronized Flag */
	while (!(STM32_RTC_CRL & (1 << 3)))
		;
}

static void prepare_rtc_write(void)
{
	/* wait for RTOFF */
	while (!(STM32_RTC_CRL & (1 << 5)))
		;
	/* set CNF bit */
	STM32_RTC_CRL |= (1 << 4);
}

static void finalize_rtc_write(void)
{
	/* reset CNF bit */
	STM32_RTC_CRL &= ~(1 << 4);
	/* wait for RTOFF */
	while (!(STM32_RTC_CRL & (1 << 5)))
		;
}

uint32_t set_rtc_alarm(unsigned delay_s, unsigned delay_us)
{
	unsigned rtc_t0, rtc_t1;

	rtc_t0 = ((uint32_t)STM32_RTC_CNTH << 16) | STM32_RTC_CNTL;
	rtc_t1 = rtc_t0 + delay_us / US_PER_RTC_TICK + delay_s * RTC_FREQ;

	prepare_rtc_write();
	/* set RTC alarm timestamp (using the 40kHz counter ) */
	STM32_RTC_ALRH = rtc_t1 >> 16;
	STM32_RTC_ALRL = rtc_t1 & 0xffff;
	/* clear RTC alarm */
	STM32_RTC_CRL &= ~2;
	/* enable RTC alarm interrupt */
	STM32_RTC_CRL |= 2;
	finalize_rtc_write();
	/* remove synchro flag */
	STM32_RTC_CRL &= ~(1 << 3);

	return rtc_t0;
}

uint32_t reset_rtc_alarm(void)
{
	uint32_t rtc_stamp;

	wait_rtc_ready();

	prepare_rtc_write();
	/* clear RTC alarm */
	STM32_RTC_CRL &= ~2;
	finalize_rtc_write();
	STM32_EXTI_PR = (1 << 17);

	rtc_stamp = ((uint32_t)STM32_RTC_CNTH << 16) | STM32_RTC_CNTL;
	return rtc_stamp;
}

static void __rtc_wakeup_irq(void)
{
	reset_rtc_alarm();
}
DECLARE_IRQ(STM32_IRQ_RTC_WAKEUP, __rtc_wakeup_irq, 1);

static void __rtc_alarm_irq(void)
{
	reset_rtc_alarm();
}
DECLARE_IRQ(STM32_IRQ_RTC_ALARM, __rtc_alarm_irq, 1);

#if defined(BOARD_snow) || defined(BOARD_spring)
/*
 * stays on HSI (8MHz), no prescaler, PLLSRC = HSI/2, PLLMUL = x4
 * no MCO                      => PLLCLK = 16 Mhz
 */
#define DESIRED_CPU_CLOCK 16000000
#define RCC_CFGR 0x00080000
#elif defined(BOARD_mccroskey)
/*
 * HSI = 8MHz, no prescaler, no MCO
 * PLLSRC = HSI/2, PLLMUL = x12 => PLLCLK = 48MHz
 * USB clock = PLLCLK
 */
#define DESIRED_CPU_CLOCK 48000000
#define RCC_CFGR 0x00680000
#else
#error "Need board-specific clock settings"
#endif
BUILD_ASSERT(CPU_CLOCK == DESIRED_CPU_CLOCK);

static void config_hispeed_clock(void)
{
	/* Ensure that HSI is ON */
	if (!(STM32_RCC_CR & (1 << 1))) {
		/* Enable HSI */
		STM32_RCC_CR |= 1 << 0;
		/* Wait for HSI to be ready */
		while (!(STM32_RCC_CR & (1 << 1)))
			;
	}

	STM32_RCC_CFGR = RCC_CFGR;
	/* Enable the PLL */
	STM32_RCC_CR |= 1 << 24;
	/* Wait for the PLL to lock */
	while (!(STM32_RCC_CR & (1 << 25)))
		;
	/* switch to SYSCLK to the PLL */
	STM32_RCC_CFGR = RCC_CFGR | 0x02;

	/* wait until the PLL is the clock source */
	while ((STM32_RCC_CFGR & 0xc) != 0x8)
		;
}

void __enter_hibernate(uint32_t seconds, uint32_t microseconds)
{
	if (seconds || microseconds)
		set_rtc_alarm(seconds, microseconds);

	/* interrupts off now */
	asm volatile("cpsid i");

	/* enable the wake up pin */
	STM32_PWR_CSR |= (1<<8);
	STM32_PWR_CR |= 0xe;
	CPU_SCB_SYSCTRL |= 0x4;
	/* go to Standby mode */
	asm("wfi");

	/* we should never reach that point */
	while (1)
		;
}

#ifdef CONFIG_LOW_POWER_IDLE

void clock_refresh_console_in_use(void)
{
}

#ifdef CONFIG_FORCE_CONSOLE_RESUME
static void enable_serial_wakeup(int enable)
{
	static uint32_t save_exticr;

	if (enable) {
		/**
		 * allow to wake up from serial port (RX on pin PA10)
		 * by setting it as a GPIO with an external interrupt.
		 */
		save_exticr = STM32_AFIO_EXTICR(10 / 4);
		STM32_AFIO_EXTICR(10 / 4) = (save_exticr & ~(0xf << 8));
	} else {
		/* serial port wake up : don't go back to sleep */
		if (STM32_EXTI_PR & (1 << 10))
			disable_sleep(SLEEP_MASK_FORCE_NO_DSLEEP);
		/* restore keyboard external IT on PC10 */
		STM32_AFIO_EXTICR(10 / 4) = save_exticr;
	}
}
#else
static void enable_serial_wakeup(int enable)
{
}
#endif

/* Idle task.  Executed when no tasks are ready to be scheduled. */
void __idle(void)
{
	timestamp_t t0, t1;
	int next_delay;
	uint32_t rtc_t0, rtc_t1;

	while (1) {
		asm volatile("cpsid i");

		t0 = get_time();
		next_delay = __hw_clock_event_get() - t0.le.lo;

		if (DEEP_SLEEP_ALLOWED && (next_delay > STOP_MODE_LATENCY)) {
			/* deep-sleep in STOP mode */

			enable_serial_wakeup(1);

			/* set deep sleep bit */
			CPU_SCB_SYSCTRL |= 0x4;

			rtc_t0 = set_rtc_alarm(0,
					       next_delay - STOP_MODE_LATENCY);
			asm("wfi");

			CPU_SCB_SYSCTRL &= ~0x4;

			enable_serial_wakeup(0);

			/* re-lock the PLL */
			config_hispeed_clock();

			/* fast forward timer according to RTC counter */
			rtc_t1 = reset_rtc_alarm();
			t1.val = t0.val + (rtc_t1 - rtc_t0) * US_PER_RTC_TICK;
			force_time(t1);
		} else {
			/* normal idle : only CPU clock stopped */
			asm("wfi");
		}
		asm volatile("cpsie i");
	}
}
#endif /* CONFIG_LOW_POWER_IDLE */

int clock_get_freq(void)
{
	return CPU_CLOCK;
}

void clock_init(void)
{
	/*
	 * The initial state :
	 *  SYSCLK from HSI (=8MHz), no divider on AHB, APB1, APB2
	 *  PLL unlocked, RTC enabled on LSE
	 */

	config_hispeed_clock();

	/* configure RTC clock */
	wait_rtc_ready();
	prepare_rtc_write();
	/* set RTC divider to /1 */
	STM32_RTC_PRLH = 0;
	STM32_RTC_PRLL = 0;
	finalize_rtc_write();
	/* setup RTC EXTINT17 to wake up us from STOP mode */
	STM32_EXTI_IMR |= (1 << 17);
	STM32_EXTI_RTSR |= (1 << 17);

	/*
	 * Our deep sleep mode is STOP mode.
	 * clear PDDS (stop mode) , set LDDS (regulator in low power mode)
	 */
	STM32_PWR_CR = (STM32_PWR_CR & ~2) | 1;

	/* Enable RTC interrupts */
	task_enable_irq(STM32_IRQ_RTC_WAKEUP);
	task_enable_irq(STM32_IRQ_RTC_ALARM);
}


