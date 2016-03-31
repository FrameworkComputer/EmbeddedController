/* Copyright (c) 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Hardware timers driver  - HPET */

#include "hpet.h"
#include "hwtimer.h"
#include "registers.h"
#include "task.h"

#define CPUTS(outstr) cputs(CC_CLOCK, outstr)
#define CPRINTS(format, args...) cprints(CC_CLOCK, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_CLOCK, format, ## args)

void __hw_clock_event_set(uint32_t deadline)
{
	HPET_TIMER_COMP(1) = HPET_MAIN_COUNTER + deadline;
	HPET_TIMER_CONF_CAP(1) |= HPET_Tn_INT_ENB_CNF;
}

uint32_t __hw_clock_event_get(void)
{
	return 0;
}

void __hw_clock_event_clear(void)
{
	HPET_TIMER_CONF_CAP(1) &= ~HPET_Tn_INT_ENB_CNF;
}

uint32_t __hw_clock_source_read(void)
{
	return HPET_MAIN_COUNTER;
}

void __hw_clock_source_set(uint32_t ts)
{
	HPET_GENERAL_CONFIG &= ~HPET_ENABLE_CNF;
	HPET_MAIN_COUNTER = 0x00;

	while (HPET_CTRL_STATUS & HPET_GEN_CONF_STATUS_BIT)
		;

	HPET_GENERAL_CONFIG |= HPET_ENABLE_CNF;
}

static void __hw_clock_source_irq(int timer_id)
{
	/* Clear interrupt */
	HPET_INTR_CLEAR = (1 << timer_id);

	/* If IRQ is from timer 0, 32-bit timer overflowed */
	process_timers(timer_id == 0);
}

void __hw_clock_source_irq_0(void)
{
	__hw_clock_source_irq(0);
}
DECLARE_IRQ(ISH_HPET_TIMER0_IRQ, __hw_clock_source_irq_0);

void __hw_clock_source_irq_1(void)
{
	__hw_clock_source_irq(1);
}
DECLARE_IRQ(ISH_HPET_TIMER1_IRQ, __hw_clock_source_irq_1);

int __hw_clock_source_init(uint32_t start_t)
{

	/*
	 * The timer can only fire interrupt when its value reaches zero.
	 * Therefore we need two timers:
	 *   - Timer 0 as free running timer
	 *   - Timer 1 as event timer
	 */

	/* Disable HPET */
	HPET_GENERAL_CONFIG &= ~HPET_ENABLE_CNF;
	HPET_MAIN_COUNTER = 0x00;

	/* Set comparator value */
	HPET_TIMER_COMP(0) = ISH_HPET_CLK_FREQ / ISH_TICKS_PER_SEC;

	/* Wait for timer to settle */
	while (HPET_CTRL_STATUS & HPET_GEN_CONF_STATUS_BIT)
		;

	/* Timer 0 - enable periodic mode */
	HPET_TIMER_CONF_CAP(0) |= HPET_Tn_TYPE_CNF;
	HPET_TIMER_CONF_CAP(0) |= HPET_Tn_32MODE_CNF;

	while (HPET_CTRL_STATUS & HPET_T0_CONF_CAP_BIT)
		;

	/* Set IRQ routing */
#if ISH_HPET_TIMER0_IRQ < 32
	HPET_TIMER_CONF_CAP(0) &= ~HPET_Tn_INT_ROUTE_CNF_MASK;
	HPET_TIMER_CONF_CAP(0) |= (ISH_HPET_TIMER0_IRQ <<
			HPET_Tn_INT_ROUTE_CNF_SHIFT);
#else
	HPET_TIMER_CONF_CAP(0) &= ~HPET_Tn_INT_ROUTE_CNF_MASK;
#endif

	while (HPET_CTRL_STATUS & HPET_T0_CONF_CAP_BIT)
		;

	/* Level interrupt */
	HPET_TIMER_CONF_CAP(0) |= HPET_Tn_INT_TYPE_CNF;
	HPET_TIMER_CONF_CAP(1) |= HPET_Tn_INT_TYPE_CNF;

	/* Unask HPET IRQ in IOAPIC */
	task_enable_irq(ISH_HPET_TIMER0_IRQ);
	task_enable_irq(ISH_HPET_TIMER1_IRQ);

	/* Enable interrupt */
	HPET_TIMER_CONF_CAP(0) |= HPET_Tn_INT_ENB_CNF;
	HPET_TIMER_CONF_CAP(1) |= HPET_Tn_INT_ENB_CNF;

	while (HPET_CTRL_STATUS & HPET_T0_CONF_CAP_BIT)
		;

	/* Enable HPET main  counter */
	HPET_GENERAL_CONFIG |= HPET_ENABLE_CNF;

	return ISH_HPET_TIMER1_IRQ; /* One shot */
}
