/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Hardware timers driver */

#include "common.h"
#include "hwtimer.h"
#include "panic.h"
#include "registers.h"
#include "task.h"
#include "timer.h"
#include "watchdog.h"

/* Divider to get microsecond for the clock */
#define CLOCKSOURCE_DIVIDER (CPU_CLOCK / SECOND)

#ifdef CHIP_VARIANT_stm32f100
#define TIM_WD_IRQ	STM32_IRQ_TIM1_UP_TIM16
#define TIM_WD		1	/* Timer to use for watchdog */
#endif

/*
 * TIM_CLOCK_MSB and TIM_CLOCK_LSB must be defined per board. The available
 * values are 2, 3, and 4. This gives us flexibility to make any of the three
 * timer as a PWM source.
 */
#define IRQ_TIM(n) STM32_CAT(STM32_IRQ_TIM, n, )
#define IRQ_MSB IRQ_TIM(TIM_CLOCK_MSB)
#define IRQ_LSB IRQ_TIM(TIM_CLOCK_LSB)

enum {
	TIM_WD_BASE	= STM32_TIM1_BASE,
};

static uint32_t last_deadline;

void __hw_clock_event_set(uint32_t deadline)
{
	last_deadline = deadline;

	if ((deadline >> 16) > STM32_TIM_CNT(TIM_CLOCK_MSB)) {
		/* first set a match on the MSB */
		STM32_TIM_CCR1(TIM_CLOCK_MSB) = deadline >> 16;
		/* disable LSB match */
		STM32_TIM_DIER(TIM_CLOCK_LSB) &= ~2;
		/* Clear the match flags */
		STM32_TIM_SR(TIM_CLOCK_MSB) = ~2;
		STM32_TIM_SR(TIM_CLOCK_LSB) = ~2;
		/* Set the match interrupt */
		STM32_TIM_DIER(TIM_CLOCK_MSB) |= 2;
	}
	/*
	 * In the unlikely case where the MSB has increased and matched
	 * the deadline MSB before we set the match interrupt, as the STM
	 * hardware timer won't trigger an interrupt, we fall back to the
	 * following LSB event code to set another interrupt.
	 */
	if ((deadline >> 16) == STM32_TIM_CNT(TIM_CLOCK_MSB)) {
		/* we can set a match on the LSB only */
		STM32_TIM_CCR1(TIM_CLOCK_LSB) = deadline & 0xffff;
		/* disable MSB match */
		STM32_TIM_DIER(TIM_CLOCK_MSB) &= ~2;
		/* Clear the match flags */
		STM32_TIM_SR(TIM_CLOCK_MSB) = ~2;
		STM32_TIM_SR(TIM_CLOCK_LSB) = ~2;
		/* Set the match interrupt */
		STM32_TIM_DIER(TIM_CLOCK_LSB) |= 2;
	}
	/*
	 * If the LSB deadline is already in the past and won't trigger an
	 * interrupt, the common code in process_timers will deal with the
	 * expired timer and automatically set the next deadline, we don't need
	 * to do anything here.
	 */
}

uint32_t __hw_clock_event_get(void)
{
	return last_deadline;
}

void __hw_clock_event_clear(void)
{
	/* Disable the match interrupts */
	STM32_TIM_DIER(TIM_CLOCK_LSB) &= ~2;
	STM32_TIM_DIER(TIM_CLOCK_MSB) &= ~2;
}

uint32_t __hw_clock_source_read(void)
{
	uint32_t hi;
	uint32_t lo;

	/* Ensure the two half-words are coherent */
	do {
		hi = STM32_TIM_CNT(TIM_CLOCK_MSB);
		lo = STM32_TIM_CNT(TIM_CLOCK_LSB);
	} while (hi != STM32_TIM_CNT(TIM_CLOCK_MSB));

	return (hi << 16) | lo;
}

void __hw_clock_source_set(uint32_t ts)
{
	STM32_TIM_CNT(TIM_CLOCK_MSB) = ts >> 16;
	STM32_TIM_CNT(TIM_CLOCK_LSB) = ts & 0xffff;
}

static void __hw_clock_source_irq(void)
{
	uint32_t stat_tim_msb = STM32_TIM_SR(TIM_CLOCK_MSB);

	/* Clear status */
	STM32_TIM_SR(TIM_CLOCK_LSB) = 0;
	STM32_TIM_SR(TIM_CLOCK_MSB) = 0;

	/*
	 * Find expired timers and set the new timer deadline
	 * signal overflow if the 16-bit MSB counter has overflowed.
	 */
	process_timers(stat_tim_msb & 0x01);
}
DECLARE_IRQ(IRQ_MSB, __hw_clock_source_irq, 1);
DECLARE_IRQ(IRQ_LSB, __hw_clock_source_irq, 1);

int __hw_clock_source_init(uint32_t start_t)
{
	/*
	 * we use 2 chained 16-bit counters to emulate a 32-bit one :
	 * TIM_CLOCK_MSB is the MSB (Slave)
	 * TIM_CLOCK_LSB is the LSB (Master)
	 */

	/* Enable TIM_CLOCK_MSB and TIM_CLOCK_LSB clocks */
	STM32_RCC_APB1ENR |= (1 << (TIM_CLOCK_MSB - 2)) |
			     (1 << (TIM_CLOCK_LSB - 2));

	/*
	 * Timer configuration : Upcounter, counter disabled, update event only
	 * on overflow.
	 */
	STM32_TIM_CR1(TIM_CLOCK_MSB) = 0x0004;
	STM32_TIM_CR1(TIM_CLOCK_LSB) = 0x0004;
	/*
	 * TIM_CLOCK_LSB (master mode) generates a periodic trigger signal on
	 * each UEV
	 */
	STM32_TIM_CR2(TIM_CLOCK_MSB) = 0x0000;
	STM32_TIM_CR2(TIM_CLOCK_LSB) = 0x0020;
	/* TIM_CLOCK_MSB (slave mode) uses TIM_CLOCK_LSB as internal trigger */
	STM32_TIM_SMCR(TIM_CLOCK_MSB) = 0x0007 | ((TIM_CLOCK_LSB - 1) << 4);
	STM32_TIM_SMCR(TIM_CLOCK_LSB) = 0x0000;
	/* Auto-reload value : 16-bit free-running counters */
	STM32_TIM_ARR(TIM_CLOCK_MSB) = 0xffff;
	STM32_TIM_ARR(TIM_CLOCK_LSB) = 0xffff;
	/*
	 * Pre-scaler value :
	 * TIM_CLOCK_LSB is counting microseconds, TIM_CLOCK_MSB is counting
	 * every TIM_CLOCK_LSB overflow.
	 */
	STM32_TIM_PSC(TIM_CLOCK_MSB) = 0;
	STM32_TIM_PSC(TIM_CLOCK_LSB) = CLOCKSOURCE_DIVIDER - 1;

	/* Reload the pre-scaler */
	STM32_TIM_EGR(TIM_CLOCK_MSB) = 0x0001;
	STM32_TIM_EGR(TIM_CLOCK_LSB) = 0x0001;

	/* setup the overflow interrupt on TIM_CLOCK_MSB */
	STM32_TIM_DIER(TIM_CLOCK_MSB) = 0x0001;
	STM32_TIM_DIER(TIM_CLOCK_LSB) = 0x0000;

	/* Start counting */
	STM32_TIM_CR1(TIM_CLOCK_MSB) |= 1;
	STM32_TIM_CR1(TIM_CLOCK_LSB) |= 1;

	/* Override the count with the start value now that counting has
	 * started. */
	__hw_clock_source_set(start_t);

	/* Enable timer interrupts */
	task_enable_irq(IRQ_MSB);
	task_enable_irq(IRQ_LSB);

	return IRQ_LSB;
}

/*
 * We don't have TIM1 on STM32L, so don't support this function for now.  TIM5
 * doesn't appear to exist in either variant, and TIM9 cannot be triggered as a
 * slave from TIM4. We could perhaps use TIM9 as our fast counter on STM32L.
 */
#ifdef CHIP_VARIANT_stm32f100

void watchdog_check(uint32_t excep_lr, uint32_t excep_sp)
{
	struct timer_ctlr *timer = (struct timer_ctlr *)TIM_WD_BASE;

	/* clear status */
	timer->sr = 0;

	watchdog_trace(excep_lr, excep_sp);
}

void IRQ_HANDLER(TIM_WD_IRQ)(void) __attribute__((naked));
void IRQ_HANDLER(TIM_WD_IRQ)(void)
{
	/* Naked call so we can extract raw LR and SP */
	asm volatile("mov r0, lr\n"
		     "mov r1, sp\n"
		     /* Must push registers in pairs to keep 64-bit aligned
		      * stack for ARM EABI.  This also conveninently saves
		      * R0=LR so we can pass it to task_resched_if_needed. */
		     "push {r0, lr}\n"
		     "bl watchdog_check\n"
		     "pop {r0, lr}\n"
		     "b task_resched_if_needed\n");
}
const struct irq_priority IRQ_BUILD_NAME(prio_, TIM_WD_IRQ, )
	__attribute__((section(".rodata.irqprio")))
		= {TIM_WD_IRQ, 0}; /* put the watchdog at the highest
					    priority */

void hwtimer_setup_watchdog(void)
{
	struct timer_ctlr *timer = (struct timer_ctlr *)TIM_WD_BASE;

	/* Enable clock */
#if TIM_WD == 1
	STM32_RCC_APB2ENR |= 1 << 11;
#else
	STM32_RCC_APB1ENR |= 1 << (TIM_WD - 2);
#endif

	/*
	 * Timer configuration : Down counter, counter disabled, update
	 * event only on overflow.
	 */
	timer->cr1 = 0x0014 | (1 << 7);

	/* TIM (slave mode) uses TIM_CLOCK_LSB as internal trigger */
	timer->smcr = 0x0007 | ((TIM_CLOCK_LSB - 1) << 4);

	/*
	 * The auto-reload value is based on the period between rollovers for
	 * TIM_CLOCK_LSB. Since TIM_CLOCK_LSB runs at 1MHz, it will overflow
	 * in 65.536ms. We divide our required watchdog period by this amount
	 * to obtain the number of times TIM_CLOCK_LSB can overflow before we
	 * generate an interrupt.
	 */
	timer->arr = timer->cnt = WATCHDOG_PERIOD_MS * 1000 / (1 << 16);

	/* count on every TIM_CLOCK_LSB overflow */
	timer->psc = 0;

	/* Reload the pre-scaler from arr when it goes below zero */
	timer->egr = 0x0000;

	/* setup the overflow interrupt */
	timer->dier = 0x0001;

	/* Start counting */
	timer->cr1 |= 1;

	/* Enable timer interrupts */
	task_enable_irq(TIM_WD_IRQ);
}

void hwtimer_reset_watchdog(void)
{
	struct timer_ctlr *timer = (struct timer_ctlr *)TIM_WD_BASE;

	timer->cnt = timer->arr;
}
#endif
