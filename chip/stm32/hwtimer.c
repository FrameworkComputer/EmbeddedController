/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Hardware timers driver */

#include <stdint.h>

#include "board.h"
#include "common.h"
#include "hwtimer.h"
#include "registers.h"
#include "task.h"

#define US_PER_SECOND 1000000

/* Divider to get microsecond for the clock */
#define CLOCKSOURCE_DIVIDER (CPU_CLOCK/US_PER_SECOND)

static uint32_t last_deadline;

void __hw_clock_event_set(uint32_t deadline)
{
	last_deadline = deadline;

	if ((deadline >> 16) == STM32_TIM_CNT(2)) {
		/* we can set a match on the LSB only */
		STM32_TIM_CCR1(3) = deadline & 0xffff;
		/* disable MSB match */
		STM32_TIM_DIER(2) &= ~2;
		/* Clear the match flags */
		STM32_TIM_SR(2) = ~2;
		STM32_TIM_SR(3) = ~2;
		/* Set the match interrupt */
		STM32_TIM_DIER(3) |= 2;
	} else if ((deadline >> 16) > STM32_TIM_CNT(2)) {
		/* first set a match on the MSB */
		STM32_TIM_CCR1(2) = deadline >> 16;
		/* disable LSB match */
		STM32_TIM_DIER(3) &= ~2;
		/* Clear the match flags */
		STM32_TIM_SR(2) = ~2;
		STM32_TIM_SR(3) = ~2;
		/* Set the match interrupt */
		STM32_TIM_DIER(2) |= 2;
	}
}

uint32_t __hw_clock_event_get(void)
{
	return last_deadline;
}

void __hw_clock_event_clear(void)
{
	/* Disable the match interrupts */
	STM32_TIM_DIER(3) &= ~2;
	STM32_TIM_DIER(2) &= ~2;
}

uint32_t __hw_clock_source_read(void)
{
	uint32_t hi;
	uint32_t lo;

	/* ensure the two half-words are coherent */
	do {
		hi = STM32_TIM_CNT(2);
		lo = STM32_TIM_CNT(3);
	} while (hi != STM32_TIM_CNT(2));

	return (hi << 16) | lo;
}

static void __hw_clock_source_irq(void)
{
	uint32_t stat_tim2 = STM32_TIM_SR(2);

	/* clear status */
	STM32_TIM_SR(3) = 0;
	STM32_TIM_SR(2) = 0;

	/*
	 * Find expired timers and set the new timer deadline
	 * signal overflow if the 16-bit MSB counter has overflowed.
	 */
	process_timers(stat_tim2 & 0x01);
}
DECLARE_IRQ(STM32_IRQ_TIM2, __hw_clock_source_irq, 1);
DECLARE_IRQ(STM32_IRQ_TIM3, __hw_clock_source_irq, 1);

int __hw_clock_source_init(void)
{
	/*
	 * we use 2 chained 16-bit counters to emulate a 32-bit one :
	 * TIM2 is the MSB (Slave)
	 * TIM3 is the LSB (Master)
	 */

	/* Enable TIM2 and TIM3 clocks */
	STM32_RCC_APB1ENR |= 0x3;

	/*
	 * Timer configuration : Upcounter, counter disabled, update event only
	 * on overflow.
	 */
	STM32_TIM_CR1(2) = 0x0004;
	STM32_TIM_CR1(3) = 0x0004;
	/* TIM3 (master mode) generates a periodic trigger signal on each UEV */
	STM32_TIM_CR2(2) = 0x0000;
	STM32_TIM_CR2(3) = 0x0020;
	/* TIM2 (slave mode) uses ITR2 as internal trigger */
	STM32_TIM_SMCR(2) = 0x0027;
	STM32_TIM_SMCR(3) = 0x0000;
	/* Auto-reload value : 16-bit free-running counters */
	STM32_TIM_ARR(2) = 0xffff;
	STM32_TIM_ARR(3) = 0xffff;
	/* Pre-scaler value :
	 * TIM3 is counting microseconds, TIM2 is counting every TIM3 overflow.
	 */
	STM32_TIM_PSC(2) = 0;
	STM32_TIM_PSC(3) = CLOCKSOURCE_DIVIDER - 1;

	/* Reload the pre-scaler */
	STM32_TIM_EGR(2) = 0x0000;
	STM32_TIM_EGR(3) = 0x0000;

	/* setup the overflow interrupt on TIM2 */
	STM32_TIM_DIER(2) = 0x0001;
	STM32_TIM_DIER(3) = 0x0000;

	/* Start counting */
	STM32_TIM_CR1(2) |= 1;
	STM32_TIM_CR1(3) |= 1;

	/* Enable timer interrupts */
	task_enable_irq(STM32_IRQ_TIM2);
	task_enable_irq(STM32_IRQ_TIM3);

	return STM32_IRQ_TIM3;
}
