/* Copyright 2013 The Chromium OS Authors. All rights reserved.
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
	MEC1322_TMR32_CNT(1) = MEC1322_TMR32_CNT(0) -
			       (0xffffffff - deadline);
	MEC1322_TMR32_CTL(1) |= BIT(5);
}

uint32_t __hw_clock_event_get(void)
{
	return MEC1322_TMR32_CNT(1) - MEC1322_TMR32_CNT(0) + 0xffffffff;
}

void __hw_clock_event_clear(void)
{
	MEC1322_TMR32_CTL(1) &= ~BIT(5);
}

uint32_t __hw_clock_source_read(void)
{
	return 0xffffffff - MEC1322_TMR32_CNT(0);
}

void __hw_clock_source_set(uint32_t ts)
{
	MEC1322_TMR32_CTL(0) &= ~BIT(5);
	MEC1322_TMR32_CNT(0) = 0xffffffff - ts;
	MEC1322_TMR32_CTL(0) |= BIT(5);
}

static void __hw_clock_source_irq(int timer_id)
{
	if (timer_id == 1)
		MEC1322_TMR32_STS(1) |= 1;
	/* If IRQ is from timer 0, 32-bit timer overflowed */
	process_timers(timer_id == 0);
}

void __hw_clock_source_irq_0(void) { __hw_clock_source_irq(0); }
DECLARE_IRQ(MEC1322_IRQ_TIMER32_0, __hw_clock_source_irq_0, 1);
void __hw_clock_source_irq_1(void) { __hw_clock_source_irq(1); }
DECLARE_IRQ(MEC1322_IRQ_TIMER32_1, __hw_clock_source_irq_1, 1);

static void configure_timer(int timer_id)
{
	uint32_t val;

	/* Ensure timer is not running */
	MEC1322_TMR32_CTL(timer_id) &= ~BIT(5);

	/* Enable timer */
	MEC1322_TMR32_CTL(timer_id) |= BIT(0);

	val = MEC1322_TMR32_CTL(timer_id);

	/* Pre-scale = 48 -> 1MHz -> Period = 1us */
	val = (val & 0xffff) | (47 << 16);

	MEC1322_TMR32_CTL(timer_id) = val;

	/* Set preload to use the full 32 bits of the timer */
	MEC1322_TMR32_PRE(timer_id) = 0xffffffff;

	/* Enable interrupt */
	MEC1322_TMR32_IEN(timer_id) |= 1;
}

int __hw_clock_source_init(uint32_t start_t)
{
	/*
	 * The timer can only fire interrupt when its value reaches zero.
	 * Therefore we need two timers:
	 *   - Timer 0 as free running timer
	 *   - Timer 1 as event timer
	 */
	configure_timer(0);
	configure_timer(1);

	/* Override the count */
	MEC1322_TMR32_CNT(0) = 0xffffffff - start_t;

	/* Auto restart */
	MEC1322_TMR32_CTL(0) |= BIT(3);

	/* Start counting in timer 0 */
	MEC1322_TMR32_CTL(0) |= BIT(5);

	/* Enable interrupt */
	task_enable_irq(MEC1322_IRQ_TIMER32_0);
	task_enable_irq(MEC1322_IRQ_TIMER32_1);
	MEC1322_INT_ENABLE(23) |= BIT(4) | BIT(5);
	MEC1322_INT_BLK_EN |= BIT(23);

	return MEC1322_IRQ_TIMER32_1;
}
