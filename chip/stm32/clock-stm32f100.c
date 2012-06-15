/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Clocks and power management settings */

#include <stdint.h>

#include "atomic.h"
#include "board.h"
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

/**
 * minimum delay to enter stop mode
 * STOP mode wakeup time with regulator in low power mode is 5 us.
 * PLL locking time is 200us.
 */
#define STOP_MODE_LATENCY 300 /* us */

/**
 * RTC clock frequency (connected to LSI clock)
 *
 * TODO: crosbug.com/p/12281 calibrate LSI frequency
 */
#define RTC_FREQ 40000 /* Hz */
#define US_PER_RTC_TICK (1000000 / RTC_FREQ)

/* On-going actions preventing to go into deep-sleep mode */
uint32_t sleep_mask = SLEEP_MASK_FORCE;

void enable_sleep(uint32_t mask)
{
	atomic_clear(&sleep_mask, mask);
}

void disable_sleep(uint32_t mask)
{
	atomic_or(&sleep_mask, mask);
}

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

uint32_t set_rtc_alarm(unsigned delay_us)
{
	unsigned rtc_t0, rtc_t1;

	rtc_t0 = ((uint32_t)STM32_RTC_CNTH << 16) | STM32_RTC_CNTL;
	rtc_t1 = rtc_t0 + delay_us / US_PER_RTC_TICK;

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

	/*
	 * stays on HSI (8MHz), no prescaler, PLLSRC = HSI/2, PLLMUL = x4
	 * no MCO                      => PLLCLK = 16 Mhz
	 */
	BUILD_ASSERT(CPU_CLOCK == 16000000);
	STM32_RCC_CFGR = 0x00080000;
	/* Enable the PLL */
	STM32_RCC_CR |= 1 << 24;
	/* Wait for the PLL to lock */
	while (!(STM32_RCC_CR & (1 << 25)))
		;
	/* switch to SYSCLK to the PLL */
	STM32_RCC_CFGR = 0x00080002;
	/* wait until the PLL is the clock source */
	while ((STM32_RCC_CFGR & 0xc) != 0x8)
		;
}

#ifdef CONFIG_LOW_POWER_IDLE
/* Idle task.  Executed when no tasks are ready to be scheduled. */
void __idle(void)
{
	timestamp_t t0, t1;
	uint32_t next_delay;
	uint32_t rtc_t0, rtc_t1;

	while (1) {
		asm volatile("cpsid i");

		t0 = get_time();
		next_delay = __hw_clock_event_get() - t0.le.lo;

		if (!sleep_mask && (next_delay > STOP_MODE_LATENCY)) {
			/* deep-sleep in STOP mode */

			/* set deep sleep bit */
			CPU_SCB_SYSCTRL |= 0x4;

			rtc_t0 = set_rtc_alarm(next_delay - STOP_MODE_LATENCY);
			asm("wfi");

			CPU_SCB_SYSCTRL &= ~0x4;
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

int clock_init(void)
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

	return EC_SUCCESS;
}

/*****************************************************************************/
/* Console commands */

static int command_sleepmask(int argc, char **argv)
{
	int off;

	if (argc >= 2) {
		off = strtoi(argv[1], NULL, 10);

		if (off)
			disable_sleep(SLEEP_MASK_FORCE);
		else
			enable_sleep(SLEEP_MASK_FORCE);
	}

	ccprintf("sleep mask: %08x\n", sleep_mask);

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(sleepmask, command_sleepmask,
			"[0|1]",
			"Display/force sleep mack",
			NULL);
