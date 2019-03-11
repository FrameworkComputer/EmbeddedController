/* Copyright 2017 The Chromium OS Authors. All rights reserved.
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
#include "tfdp_chip.h"

void __hw_clock_event_set(uint32_t deadline)
{
	MCHP_TMR32_CNT(1) = MCHP_TMR32_CNT(0) -
			       (0xffffffff - deadline);
	MCHP_TMR32_CTL(1) |= BIT(5);
}

uint32_t __hw_clock_event_get(void)
{
	return MCHP_TMR32_CNT(1) - MCHP_TMR32_CNT(0) + 0xffffffff;
}

void __hw_clock_event_clear(void)
{
	MCHP_TMR32_CTL(1) &= ~BIT(5);
}

uint32_t __hw_clock_source_read(void)
{
	return 0xffffffff - MCHP_TMR32_CNT(0);
}

void __hw_clock_source_set(uint32_t ts)
{
	MCHP_TMR32_CTL(0) &= ~BIT(5);
	MCHP_TMR32_CNT(0) = 0xffffffff - ts;
	MCHP_TMR32_CTL(0) |= BIT(5);
}

/*
 * Always clear both timer and aggregator status
 */
static void __hw_clock_source_irq(int timer_id)
{
	MCHP_TMR32_STS(timer_id & 0x01) |= 1;
	MCHP_INT_SOURCE(MCHP_TMR32_GIRQ) =
			MCHP_TMR32_GIRQ_BIT(timer_id & 0x01);

	/* If IRQ is from timer 0, 32-bit timer overflowed */
	process_timers(timer_id == 0);
}

void __hw_clock_source_irq_0(void) { __hw_clock_source_irq(0); }
DECLARE_IRQ(MCHP_IRQ_TIMER32_0, __hw_clock_source_irq_0, 1);
void __hw_clock_source_irq_1(void) { __hw_clock_source_irq(1); }
DECLARE_IRQ(MCHP_IRQ_TIMER32_1, __hw_clock_source_irq_1, 1);

static void configure_timer(int timer_id)
{
	uint32_t val;

	/* Ensure timer is not running */
	MCHP_TMR32_CTL(timer_id) &= ~BIT(5);

	/* Enable timer */
	MCHP_TMR32_CTL(timer_id) |= BIT(0);

	val = MCHP_TMR32_CTL(timer_id);

	/* Pre-scale = 48 -> 1MHz -> Period = 1us */
	val = (val & 0xffff) | (47 << 16);

	MCHP_TMR32_CTL(timer_id) = val;

	/* Set preload to use the full 32 bits of the timer */
	MCHP_TMR32_PRE(timer_id) = 0xffffffff;

	/* Enable interrupt */
	MCHP_TMR32_IEN(timer_id) |= 1;
}

int __hw_clock_source_init(uint32_t start_t)
{
	MCHP_PCR_SLP_DIS_DEV_MASK(3, MCHP_PCR_SLP_EN3_BTMR32_0 +
			MCHP_PCR_SLP_EN3_BTMR32_1);

	/*
	 * The timer can only fire interrupt when its value reaches zero.
	 * Therefore we need two timers:
	 *   - Timer 0 as free running timer
	 *   - Timer 1 as event timer
	 */
	configure_timer(0);
	configure_timer(1);

	/* Override the count */
	MCHP_TMR32_CNT(0) = 0xffffffff - start_t;

	/* Auto restart */
	MCHP_TMR32_CTL(0) |= BIT(3);

	/* Start counting in timer 0 */
	MCHP_TMR32_CTL(0) |= BIT(5);

	/* Enable interrupt */
	task_enable_irq(MCHP_IRQ_TIMER32_0);
	task_enable_irq(MCHP_IRQ_TIMER32_1);
	MCHP_INT_ENABLE(MCHP_TMR32_GIRQ) = MCHP_TMR32_GIRQ_BIT(0) +
			MCHP_TMR32_GIRQ_BIT(1);
	/*
	 * Not needed when using direct mode interrupts
	 * MCHP_INT_BLK_EN |= BIT(MCHP_TMR32_GIRQ);
	 */
	return MCHP_IRQ_TIMER32_1;
}
