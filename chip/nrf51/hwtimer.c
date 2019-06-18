/* Copyright 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * Hardware timers driver.
 *
 * nRF51x has one fully functional hardware counter, but 4 stand-alone
 * capture/compare (CC) registers.
 */

#include "common.h"
#include "console.h"
#include "hooks.h"
#include "hwtimer.h"
#include "registers.h"
#include "task.h"
#include "util.h"

#define CPUTS(outstr) cputs(CC_CLOCK, outstr)
#define CPRINTF(format, args...) cprintf(CC_CLOCK, format, ## args)
#define CPRINTS(format, args...) cprints(CC_CLOCK, format, ## args)

/*
 * capture/compare (CC) registers:
 *   CC_INTERRUPT -- used to interrupt next clock event.
 *   CC_CURRENT -- used to capture the current value.
 *   CC_OVERFLOW -- used to detect overflow on virtual timer (not hardware).
 */

#define CC_INTERRUPT  0
#define CC_CURRENT    1
#define CC_OVERFLOW   2

/* The nRF51 has 3 timers, use HWTIMER to specify which one is used here. */
#define HWTIMER       0

static uint32_t last_deadline;  /* cache of event set */

/*
 * The nRF51x timer cannot be set to a specified value (reset to zero only).
 * Thus, we have to use a variable "shift" to maintain the offset between the
 * hardware value and virtual clock value.
 *
 * Once __hw_clock_source_set(ts) is called, the shift will be like:
 *
 *   virtual time  ------------------------------------------------
 *                  <----------> ^
 *                      shift    | ts
 *               0 |             |
 *   hardware      v
 *   counter time  ------------------------------------------------
 *
 *
 * Below diagram shows what it is when overflow happens.
 *
 *                       | now                                | prev_read
 *                       v                                    v
 *   virtual time  ------------------------------------------------
 *                 ---->                                    <------
 *                 shift                                      shift
 *                                                         |
 *   hardware                                              v
 *   counter time  ------------------------------------------------
 *
 */
static uint32_t shift;

void __hw_clock_event_set(uint32_t deadline)
{
	last_deadline = deadline;
	NRF51_TIMER_CC(HWTIMER, CC_INTERRUPT) = deadline - shift;

	/* enable interrupt */
	NRF51_TIMER_INTENSET(HWTIMER) =
		1 << NRF51_TIMER_COMPARE_BIT(CC_INTERRUPT);
}

uint32_t __hw_clock_event_get(void)
{
	return last_deadline;
}

void __hw_clock_event_clear(void)
{
	/* disable interrupt */
	NRF51_TIMER_INTENCLR(HWTIMER) =
		1 << NRF51_TIMER_COMPARE_BIT(CC_INTERRUPT);
}

uint32_t __hw_clock_source_read(void)
{
	/* to capture the current value */
	NRF51_TIMER_CAPTURE(HWTIMER, CC_CURRENT) = 1;
	return NRF51_TIMER_CC(HWTIMER, CC_CURRENT) + shift;
}

void __hw_clock_source_set(uint32_t ts)
{
	shift = ts;

	/* reset counter to zero */
	NRF51_TIMER_STOP(HWTIMER) = 1;
	NRF51_TIMER_CLEAR(HWTIMER) = 1;

	/* So that no interrupt until next __hw_clock_event_set() */
	NRF51_TIMER_CC(HWTIMER, CC_INTERRUPT) = ts - 1;

	/* Update the overflow point */
	NRF51_TIMER_CC(HWTIMER, CC_OVERFLOW) = 0 - shift;

	/* Start the timer again */
	NRF51_TIMER_START(HWTIMER) = 1;
}


/* Interrupt handler for timer */
void timer_irq(void)
{
	int overflow = 0;

	/* clear status */
	NRF51_TIMER_COMPARE(HWTIMER, CC_INTERRUPT) = 0;

	if (NRF51_TIMER_COMPARE(HWTIMER, CC_OVERFLOW)) {
		NRF51_TIMER_COMPARE(HWTIMER, CC_OVERFLOW) = 0;
		overflow = 1;
	}

	process_timers(overflow);
}

/* DECLARE_IRQ doesn't like the NRF51_PERID_TIMER(n) macro */
BUILD_ASSERT(NRF51_PERID_TIMER(HWTIMER) == NRF51_PERID_TIMER0);
DECLARE_IRQ(NRF51_PERID_TIMER0, timer_irq, 1);

int __hw_clock_source_init(uint32_t start_t)
{

	/* Start the high freq crystal oscillator */
	NRF51_CLOCK_HFCLKSTART = 1;
	/* TODO: check if the crystal oscillator is running (HFCLKSTAT) */

	/* 32-bit timer mode */
	NRF51_TIMER_MODE(HWTIMER) = NRF51_TIMER_MODE_TIMER;
	NRF51_TIMER_BITMODE(HWTIMER) = NRF51_TIMER_BITMODE_32;

	/*
	 * The external crystal oscillator is 16MHz (HFCLK).
	 * Set the prescaler to 16 so that the timer counter is increasing
	 * every micro-second (us).
	 */
	NRF51_TIMER_PRESCALER(HWTIMER) = 4;  /* actual value is 2**4 = 16 */

	/* Not to trigger interrupt until __hw_clock_event_set() is called. */
	NRF51_TIMER_CC(HWTIMER, CC_INTERRUPT) = 0xffffffff;

	/* Set to 0 so that the next overflow can trigger timer_irq(). */
	NRF51_TIMER_CC(HWTIMER, CC_OVERFLOW) = 0;
	NRF51_TIMER_INTENSET(HWTIMER) =
		1 << NRF51_TIMER_COMPARE_BIT(CC_OVERFLOW);

	/* Clear the timer counter */
	NRF51_TIMER_CLEAR(HWTIMER) = 1;

	/* Override the count with the start value now that counting has
	 * started. */
	__hw_clock_source_set(start_t);

	/* Enable interrupt */
	task_enable_irq(NRF51_PERID_TIMER(HWTIMER));

	/* Start the timer */
	NRF51_TIMER_START(HWTIMER) = 1;

	return NRF51_PERID_TIMER(HWTIMER);
}

