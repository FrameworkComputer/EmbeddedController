/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * Hardware timers driver.
 *
 * nRF51x has one hardware counter, but 4 stand-alone capture/compare (CC)
 * registers.
 *
 *   CC(0) -- used to interrupt next clock event.
 *   CC(1) -- used to capture the current value.
 *   CC(2) -- used to detect overflow on virtual timer (not hardware).
 */

#include "common.h"
#include "console.h"
#include "hooks.h"
#include "hwtimer.h"
#include "registers.h"
#include "task.h"

#define CPUTS(outstr) cputs(CC_CLOCK, outstr)
#define CPRINTF(format, args...) cprintf(CC_CLOCK, format, ## args)
#define CPRINTS(format, args...) cprints(CC_CLOCK, format, ## args)


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
	NRF51_TIMER0_CC0 = deadline - shift;

	/* enable interrupt */
	NRF51_TIMER0_INTENSET = 1 << NRF51_TIMER_COMPARE0_BIT;
}

uint32_t __hw_clock_event_get(void)
{
	return last_deadline;
}

void __hw_clock_event_clear(void)
{
	/* disable interrupt */
	NRF51_TIMER0_INTENCLR = 1 << NRF51_TIMER_COMPARE0_BIT;
}

uint32_t __hw_clock_source_read(void)
{
	/* to capture the current value */
	NRF51_TIMER0_CAPTURE1 = 1;
	return NRF51_TIMER0_CC1 + shift;
}

void __hw_clock_source_set(uint32_t ts)
{
	shift = ts;

	/* reset counter to zero */
	NRF51_TIMER0_STOP = 1;
	NRF51_TIMER0_CLEAR = 1;

	/* So that no interrupt until next __hw_clock_event_set() */
	NRF51_TIMER0_CC0 = ts - 1;

	/* Update the overflow point */
	NRF51_TIMER0_CC2 = 0 - shift;

	/* Start the timer again */
	NRF51_TIMER0_START = 1;
}


/* Interrupt handler for timer */
void timer_irq(void)
{
	int overflow = 0;

	/* clear status */
	NRF51_TIMER0_COMPARE0 = 0;

	if (NRF51_TIMER0_COMPARE2) {
		NRF51_TIMER0_COMPARE2 = 0;
		overflow = 1;
	}

	process_timers(overflow);
}
DECLARE_IRQ(NRF51_PERID_TIMER0, timer_irq, 1);


int __hw_clock_source_init(uint32_t start_t)
{
	/* Start the high freq crystal oscillator */
	NRF51_CLOCK_HFCLKSTART = 1;
	/* TODO: check if the crystal oscillator is running (HFCLKSTAT) */

	/* 32-bit timer mode */
	NRF51_TIMER0_MODE = NRF51_TIMER0_MODE_TIMER;
	NRF51_TIMER0_BITMODE = NRF51_TIMER0_BITMODE_32;

	/*
	 * The external crystal oscillator is 16MHz (HFCLK).
	 * Set the prescaler to 16 so that the timer counter is increasing
	 * every micro-second (us).
	 */
	NRF51_TIMER0_PRESCALER = 4;  /* actual value is 2**4 = 16 */

	/* Not to trigger interrupt until __hw_clock_event_set() is called. */
	NRF51_TIMER0_CC0 = 0xffffffff;

	/* Set to 0 so that the next overflow can trigger timer_irq(). */
	NRF51_TIMER0_CC2 = 0;
	NRF51_TIMER0_INTENSET = 1 << NRF51_TIMER_COMPARE2_BIT;

	/* Clear the timer counter */
	NRF51_TIMER0_CLEAR = 1;

	/* Override the count with the start value now that counting has
	 * started. */
	__hw_clock_source_set(start_t);

	/* Enable interrupt */
	task_enable_irq(NRF51_PERID_TIMER0);

	/* Start the timer */
	NRF51_TIMER0_START = 1;

	return NRF51_PERID_TIMER0;
}

