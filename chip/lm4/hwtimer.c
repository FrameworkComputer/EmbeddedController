/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Hardware timers driver */

#include "clock.h"
#include "common.h"
#include "hooks.h"
#include "hwtimer.h"
#include "registers.h"
#include "task.h"
#include "timer.h"

void __hw_clock_event_set(uint32_t deadline)
{
	/* set the match on the deadline */
	LM4_TIMER_TAMATCHR(6) = 0xffffffff - deadline;
	/* Set the match interrupt */
	LM4_TIMER_IMR(6) |= 0x10;
}

uint32_t __hw_clock_event_get(void)
{
	return 0xffffffff - LM4_TIMER_TAMATCHR(6);
}

void __hw_clock_event_clear(void)
{
	/* Disable the match interrupt */
	LM4_TIMER_IMR(6) &= ~0x10;
}

uint32_t __hw_clock_source_read(void)
{
	return 0xffffffff - LM4_TIMER_TAV(6);
}

void __hw_clock_source_set(uint32_t ts)
{
	LM4_TIMER_TAV(6) = 0xffffffff - ts;
}

static void __hw_clock_source_irq(void)
{
	uint32_t status = LM4_TIMER_RIS(6);

	/* Clear interrupt */
	LM4_TIMER_ICR(6) = status;

	/*
	 * Find expired timers and set the new timer deadline; check the IRQ
	 * status to determine if the free-running counter overflowed.
	 */
	process_timers(status & 0x01);
}
DECLARE_IRQ(LM4_IRQ_TIMERW0A, __hw_clock_source_irq, 1);

static void update_prescaler(void)
{
	/*
	 * Set the prescaler to increment every microsecond.  This takes
	 * effect immediately, because the TAILD bit in TAMR is clear.
	 */
	LM4_TIMER_TAPR(6) = clock_get_freq() / SECOND;
}
DECLARE_HOOK(HOOK_FREQ_CHANGE, update_prescaler, HOOK_PRIO_DEFAULT);

int __hw_clock_source_init(uint32_t start_t)
{
	volatile uint32_t scratch __attribute__((unused));

	/*
	 * Use WTIMER0 (timer 6) configured as a free running counter with 1 us
	 * period.
	 */

	/* Enable WTIMER0 clock */
	LM4_SYSTEM_RCGCWTIMER |= 1;
	/* wait 3 clock cycles before using the module */
	scratch = LM4_SYSTEM_RCGCWTIMER;

	/* Ensure timer is disabled : TAEN = TBEN = 0 */
	LM4_TIMER_CTL(6) &= ~0x101;
	/* Set overflow interrupt */
	LM4_TIMER_IMR(6) = 0x1;
	/* 32-bit timer mode */
	LM4_TIMER_CFG(6) = 4;

	/* Set initial prescaler */
	update_prescaler();

	/* Periodic mode, counting down */
	LM4_TIMER_TAMR(6) = 0x22;
	/* Use the full 32-bits of the timer */
	LM4_TIMER_TAILR(6) = 0xffffffff;
	/* Starts counting in timer A */
	LM4_TIMER_CTL(6) |= 0x1;

	/*
	 * Override the count with the start value now that counting has
	 * started.
	 */
	__hw_clock_source_set(start_t);

	/* Enable interrupt */
	task_enable_irq(LM4_IRQ_TIMERW0A);

	return LM4_IRQ_TIMERW0A;
}
