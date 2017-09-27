/* Copyright (c) 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Hardware timers driver  - HPET */

#include "console.h"
#include "hpet.h"
#include "hwtimer.h"
#include "registers.h"
#include "task.h"
#include "util.h"

#ifdef CONFIG_ISH_30
#define CLOCK_FACTOR 12
#endif

#define CPUTS(outstr) cputs(CC_CLOCK, outstr)
#define CPRINTS(format, args...) cprints(CC_CLOCK, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_CLOCK, format, ## args)

static uint32_t last_deadline;

/* TODO: Conform to EC API
 * ISH supports 32KHz and 12MHz clock sources.
 * EC expects timer value in 1MHz.
 * Scale the values and support it.
 */

void __hw_clock_event_set(uint32_t deadline)
{
	last_deadline = deadline;
#ifdef CONFIG_ISH_30
	HPET_TIMER_COMP(1) = deadline * CLOCK_FACTOR;
#else
	HPET_TIMER_COMP(1) = deadline;
#endif
	HPET_TIMER_CONF_CAP(1) |= HPET_Tn_INT_ENB_CNF;
}

uint32_t __hw_clock_event_get(void)
{
	return last_deadline;
}

void __hw_clock_event_clear(void)
{
	HPET_TIMER_CONF_CAP(1) &= ~HPET_Tn_INT_ENB_CNF;
}

uint32_t __hw_clock_source_read(void)
{
#ifdef CONFIG_ISH_30
	uint64_t tmp = HPET_MAIN_COUNTER_64;
	uint32_t hi = tmp >> 32;
	uint32_t lo = tmp;
	uint32_t q, r;
	const uint32_t d = CLOCK_FACTOR;
	asm ("divl %4" : "=d" (r), "=a" (q) : "0" (hi), "1" (lo), "rm" (d) : "cc");
	return q;
#else
	return HPET_MAIN_COUNTER;
#endif
}

void __hw_clock_source_set(uint32_t ts)
{
	HPET_GENERAL_CONFIG &= ~HPET_ENABLE_CNF;
#ifdef CONFIG_ISH_30
	HPET_MAIN_COUNTER_64 = (uint64_t)ts * CLOCK_FACTOR;
#else
	HPET_MAIN_COUNTER = ts;
#endif
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

	uint32_t timer0_config = 0x00000000;
	uint32_t timer1_config = 0x00000000;

	/* Disable HPET */
	HPET_GENERAL_CONFIG &= ~HPET_ENABLE_CNF;
#ifdef CONFIG_ISH_30
	HPET_MAIN_COUNTER_64 = (uint64_t)start_t * CLOCK_FACTOR;
#else
	HPET_MAIN_COUNTER = start_t;
#endif

#ifdef CONFIG_ISH_30
	/*
	 * Set comparator value. HMC will operate in 64 bit mode.
	 * HMC is 12MHz, Hence set COMP to 12x of 1MHz.
	 */
	HPET_TIMER_COMP_64(0) = (uint64_t)CLOCK_FACTOR << 32; /*0xC00000000ULL;*/
#else
	/* Set comparator value */
	HPET_TIMER_COMP(0) = 0XFFFFFFFF;
#endif
	/* Timer 0 - enable periodic mode */
	timer0_config |= HPET_Tn_TYPE_CNF;
#ifdef CONFIG_ISH_30
	/* TIMER0 in 64-bit mode */
	timer0_config &= ~HPET_Tn_32MODE_CNF;
#else
	/*TIMER0 in 32-bit mode*/
	timer0_config |= HPET_Tn_32MODE_CNF;
#endif
	timer0_config |= HPET_Tn_VAL_SET_CNF;

	/* Timer 0 - IRQ routing, no need IRQ set for HPET0 */
	timer0_config &= ~HPET_Tn_INT_ROUTE_CNF_MASK;

	/* Timer 1 - IRQ routing */
	timer1_config &= ~HPET_Tn_INT_ROUTE_CNF_MASK;
	timer1_config |= (ISH_HPET_TIMER1_IRQ <<
				HPET_Tn_INT_ROUTE_CNF_SHIFT);

	/* Level triggered interrupt */
	timer0_config |= HPET_Tn_INT_TYPE_CNF;
	timer1_config |= HPET_Tn_INT_TYPE_CNF;

	/* Enable interrupt */
	timer0_config |= HPET_Tn_INT_ENB_CNF;
	timer1_config |= HPET_Tn_INT_ENB_CNF;

	/* Unask HPET IRQ in IOAPIC */
	task_enable_irq(ISH_HPET_TIMER0_IRQ);
	task_enable_irq(ISH_HPET_TIMER1_IRQ);

	/* Set timer 0/1 config */
	HPET_TIMER_CONF_CAP(0) |= timer0_config;
	HPET_TIMER_CONF_CAP(1) |= timer1_config;

#ifdef CONFIG_ISH_40
	/* Wait for timer to settle. required for ISH 4 */
	while (HPET_CTRL_STATUS & HPET_T_CONF_CAP_BIT)
		;
#endif

	/*
	 * LEGACY_RT_CNF for HPET1 interrupt routing
	 * and enable overall HPET counter/interrupts.
	 */
	HPET_GENERAL_CONFIG |= (HPET_ENABLE_CNF | HPET_LEGACY_RT_CNF);

	return ISH_HPET_TIMER1_IRQ;
}
