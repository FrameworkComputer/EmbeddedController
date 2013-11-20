/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Hardware timers driver */

#include "cpu.h"
#include "common.h"
#include "hooks.h"
#include "hwtimer.h"
#include "registers.h"
#include "task.h"
#include "timer.h"
#include "util.h"
#include "watchdog.h"

/* 128us (2^7 us) between 2 ticks */
#define TICK_INTERVAL_LOG2  7

#define TICK_INTERVAL      (1 << TICK_INTERVAL_LOG2)
#define TICK_INTERVAL_MASK (TICK_INTERVAL - 1)

/*
 * Tick interval must fit in one byte, and must be greater than two
 * so that the duty cycle does not equal the cycle time (IT83XX_TMR_DCR_B0 must
 * be less than IT83XX_TMR_CTR_B0).
 */
BUILD_ASSERT(TICK_INTERVAL < 256 && TICK_INTERVAL > 2);

static volatile uint32_t time_us;

/*
 * Next event time of 0 represents "no event set". But, when we actually want
 * to trigger when the event time is 0, it is handled implicitly by calling
 * process_timers(1) when the timer value rolls over.
 */
static uint32_t next_event_time;

void __hw_clock_event_set(uint32_t deadline)
{
	next_event_time = deadline;
}

uint32_t __hw_clock_event_get(void)
{
	return next_event_time;
}

void __hw_clock_event_clear(void)
{
	next_event_time = 0;
}

uint32_t __hw_clock_source_read(void)
{
	return time_us;
}

void __hw_clock_source_set(uint32_t ts)
{
	time_us = ts & TICK_INTERVAL_MASK;
}


static void __hw_clock_source_irq(void)
{
#ifdef CONFIG_WATCHDOG
	/* Determine interrupt number. */
	int irq = IT83XX_INTC_IVCT3 - 16;
#endif

	/*
	 * If this is a SW interrupt, then process the timers, but don't
	 * increment the time_us.
	 */
	if (get_itype() & 8) {
		process_timers(0);
		return;
	}

#ifdef CONFIG_WATCHDOG
	/*
	 * Both the external timer for the watchdog warning and the HW timer
	 * go through this irq. So, if this interrupt was caused by watchdog
	 * warning timer, then call that function.
	 */
	if (irq == IT83XX_IRQ_EXT_TIMER3) {
		watchdog_warning_irq();
		return;
	}
#endif

	/* clear interrupt status */
	task_clear_pending_irq(IT83XX_IRQ_TMR_B0);

	time_us += TICK_INTERVAL;

	/*
	 * Find expired timers and set the new timer deadline; check the IRQ
	 * status to determine if the free-running counter overflowed. Note
	 * since each tick is greater than 1us and events can be set in
	 * increments of 1us, in order to find expired timers we have to
	 * check two conditions: the current time is exactly the next event
	 * time, or this tick just caused us to pass the next event time.
	 */
	if (time_us == next_event_time ||
			(time_us-TICK_INTERVAL) ==
					(next_event_time & ~TICK_INTERVAL_MASK))
		process_timers(0);
	else if (time_us == 0)
		process_timers(1);
}
DECLARE_IRQ(IT83XX_IRQ_TMR_B0, __hw_clock_source_irq, 1);

static void setup_gpio(void)
{
	/* TMB0 enabled */
	IT83XX_GPIO_GRC2 |= 0x04;

	/* Pin muxing (TMB0) */
	IT83XX_GPIO_GPCRF0 = 0x00;
}

static void hw_timer_enable_int(void)
{
	/* clear interrupt status */
	task_clear_pending_irq(IT83XX_IRQ_TMR_B0);

	/* enable interrupt B0 */
	task_enable_irq(IT83XX_IRQ_TMR_B0);
}

int __hw_clock_source_init(uint32_t start_t)
{
	__hw_clock_source_set(start_t);

	/* GPIO module should do this. */
	setup_gpio();

#if PLL_CLOCK == 48000000
	/* Set prescaler divider value (PRSC0 = /8). */
	IT83XX_TMR_PRSC = 0x04;

	/* Tim B: 8  bit pulse mode, 8MHz clock. */
	IT83XX_TMR_GCSMS = 0x01;
#else
#error "Support only for PLL clock speed of 48MHz."
#endif

	/* Set timer B to use PRSC0. */
	IT83XX_TMR_CCGSR = 0x00;

	/*
	 * Set the 8-bit cycle time, duty time for timer B. Note 0 < DCR < CTR
	 * in order for timer interrupt to properly fire when cycle time is
	 * reached.
	 */
	IT83XX_TMR_CTR_B0 = TICK_INTERVAL - 1;
	IT83XX_TMR_DCR_B0 = 0x01;

	/* Enable the cycle time interrupt for timer B0. */
	IT83XX_TMR_TMRIE |= 0x10;

	hw_timer_enable_int();

	/* Enable TMR clock counter. */
	IT83XX_TMR_TMRCE |= 0x02;

	return IT83XX_IRQ_TMR_B0;
}

void udelay(unsigned us)
{
	/*
	 * When WNCKR register is set, the CPU pauses until a low to
	 * high transition on an internal 65kHz clock (~15.25us). We need to
	 * make sure though that we don't ever delay less than the requested
	 * amount, so we always have to add an extra wait.
	 *
	 * TODO: This code has a few limitations, the math isn't exact so
	 * the larger the delay the farther off it will be, it uses a divide,
	 * and the resolution is only about 15us.
	 */
	int waits = us*4/61 + 1;
	while (waits-- >= 0)
		IT83XX_GCTRL_WNCKR = 0;
}
