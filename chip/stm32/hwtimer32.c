/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Hardware 32-bit timer driver */

#include "clock.h"
#include "common.h"
#include "hooks.h"
#include "hwtimer.h"
#include "panic.h"
#include "registers.h"
#include "task.h"
#include "timer.h"
#include "watchdog.h"

#define IRQ_TIM(n) CONCAT2(STM32_IRQ_TIM, n)

void __hw_clock_event_set(uint32_t deadline)
{
	/* set the match on the deadline */
	STM32_TIM32_CCR1(TIM_CLOCK32) = deadline;
	/* Clear the match flags */
	STM32_TIM_SR(TIM_CLOCK32) = ~2;
	/* Set the match interrupt */
	STM32_TIM_DIER(TIM_CLOCK32) |= 2;
}

uint32_t __hw_clock_event_get(void)
{
	return STM32_TIM32_CCR1(TIM_CLOCK32);
}

void __hw_clock_event_clear(void)
{
	/* Disable the match interrupts */
	STM32_TIM_DIER(TIM_CLOCK32) &= ~2;
}

uint32_t __hw_clock_source_read(void)
{
	return STM32_TIM32_CNT(TIM_CLOCK32);
}

void __hw_clock_source_set(uint32_t ts)
{
	STM32_TIM32_CNT(TIM_CLOCK32) = ts;
}

void __hw_clock_source_irq(void)
{
	uint32_t stat_tim = STM32_TIM_SR(TIM_CLOCK32);

	/* Clear status */
	STM32_TIM_SR(TIM_CLOCK32) = 0;

	/*
	 * Find expired timers and set the new timer deadline
	 * signal overflow if the update interrupt flag is set.
	 */
	process_timers(stat_tim & 0x01);
}
DECLARE_IRQ(IRQ_TIM(TIM_CLOCK32), __hw_clock_source_irq, 1);

void __hw_timer_enable_clock(int n, int enable)
{
	volatile uint32_t *reg;
	uint32_t mask = 0;

	/*
	 * Mapping of timers to reg/mask is split into a few different ranges,
	 * some specific to individual chips.
	 */
#if defined(CHIP_FAMILY_STM32F) || defined(CHIP_FAMILY_STM32F0)
	if (n == 1) {
		reg = &STM32_RCC_APB2ENR;
		mask = STM32_RCC_PB2_TIM1;
	}
#elif defined(CHIP_FAMILY_STM32L)
	if (n >= 9 && n <= 11) {
		reg = &STM32_RCC_APB2ENR;
		mask = STM32_RCC_PB2_TIM9 << (n - 9);
	}
#endif

#if defined(CHIP_FAMILY_STM32F0)
	if (n >= 15 && n <= 17) {
		reg = &STM32_RCC_APB2ENR;
		mask = STM32_RCC_PB2_TIM15 << (n - 15);
	}
#endif

#if defined(CHIP_FAMILY_STM32F0) || defined(CHIP_FAMILY_STM32F3)
	if (n == 14) {
		reg = &STM32_RCC_APB1ENR;
		mask = STM32_RCC_PB1_TIM14;
	}
#endif

#if defined(CHIP_FAMILY_STM32F3)
	if (n == 12 || n == 13) {
		reg = &STM32_RCC_APB1ENR;
		mask = STM32_RCC_PB1_TIM12 << (n - 12);
	}
	if (n == 18) {
		reg = &STM32_RCC_APB1ENR;
		mask = STM32_RCC_PB1_TIM18;
	}
	if (n == 19) {
		reg = &STM32_RCC_APB2ENR;
		mask = STM32_RCC_PB2_TIM19;
	}
#endif
	if (n >= 2 && n <= 7) {
		reg = &STM32_RCC_APB1ENR;
		mask = STM32_RCC_PB1_TIM2 << (n - 2);
	}

	if (!mask)
		return;

	if (enable)
		*reg |= mask;
	else
		*reg &= ~mask;
}

static void update_prescaler(void)
{
	/*
	 * Pre-scaler value :
	 * the timer is incrementing every microsecond
	 *
	 * This will take effect at the next update event (when the current
	 * prescaler counter ticks down, or if forced via EGR).
	 */
	STM32_TIM_PSC(TIM_CLOCK32) = (clock_get_freq() / SECOND) - 1;

#ifdef CONFIG_WATCHDOG_HELP
	/* Watchdog timer runs at 1KHz */
	STM32_TIM_PSC(TIM_WATCHDOG) = (clock_get_freq() / SECOND * MSEC) - 1;
#endif  /* CONFIG_WATCHDOG_HELP */
}
DECLARE_HOOK(HOOK_FREQ_CHANGE, update_prescaler, HOOK_PRIO_DEFAULT);

int __hw_clock_source_init(uint32_t start_t)
{
	/* Enable TIM peripheral block clocks */
	__hw_timer_enable_clock(TIM_CLOCK32, 1);

	/*
	 * Timer configuration : Upcounter, counter disabled, update event only
	 * on overflow.
	 */
	STM32_TIM_CR1(TIM_CLOCK32) = 0x0004;
	/* No special configuration */
	STM32_TIM_CR2(TIM_CLOCK32) = 0x0000;
	STM32_TIM_SMCR(TIM_CLOCK32) = 0x0000;

	/* Auto-reload value : 32-bit free-running counter */
	STM32_TIM32_ARR(TIM_CLOCK32) = 0xffffffff;

	/* Update prescaler */
	update_prescaler();

	/* Reload the pre-scaler */
	STM32_TIM_EGR(TIM_CLOCK32) = 0x0001;

	/* Set up the overflow interrupt */
	STM32_TIM_DIER(TIM_CLOCK32) = 0x0001;

	/* Start counting */
	STM32_TIM_CR1(TIM_CLOCK32) |= 1;

	/* Override the count with the start value now that counting has
	 * started. */
	__hw_clock_source_set(start_t);

	/* Enable timer interrupts */
	task_enable_irq(IRQ_TIM(TIM_CLOCK32));

	return IRQ_TIM(TIM_CLOCK32);
}

#ifdef CONFIG_WATCHDOG_HELP

#define IRQ_WD IRQ_TIM(TIM_WATCHDOG)

void watchdog_check(uint32_t excep_lr, uint32_t excep_sp)
{
	/* clear status */
	STM32_TIM_SR(TIM_WATCHDOG) = 0;

	watchdog_trace(excep_lr, excep_sp);
}

void IRQ_HANDLER(IRQ_WD)(void) __attribute__((naked));
void IRQ_HANDLER(IRQ_WD)(void)
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
const struct irq_priority IRQ_PRIORITY(IRQ_WD)
	__attribute__((section(".rodata.irqprio")))
		= {IRQ_WD, 0}; /* put the watchdog at the highest
					    priority */

void hwtimer_setup_watchdog(void)
{
	/* Enable clock */
	__hw_timer_enable_clock(TIM_WATCHDOG, 1);

	/*
	 * Timer configuration : Up counter, counter disabled, update
	 * event only on overflow.
	 */
	STM32_TIM_CR1(TIM_WATCHDOG) = 0x0004;
	/* No special configuration */
	STM32_TIM_CR2(TIM_WATCHDOG) = 0x0000;
	STM32_TIM_SMCR(TIM_WATCHDOG) = 0x0000;

	/* AUto-reload value */
	STM32_TIM_ARR(TIM_WATCHDOG) = CONFIG_AUX_TIMER_PERIOD_MS;

	/* Update prescaler */
	update_prescaler();

	/* Reload the pre-scaler */
	STM32_TIM_EGR(TIM_WATCHDOG) = 0x0001;

	/* setup the overflow interrupt */
	STM32_TIM_DIER(TIM_WATCHDOG) = 0x0001;
	STM32_TIM_SR(TIM_WATCHDOG) = 0;

	/* Start counting */
	STM32_TIM_CR1(TIM_WATCHDOG) |= 1;

	/* Enable timer interrupts */
	task_enable_irq(IRQ_WD);
}

void hwtimer_reset_watchdog(void)
{
	STM32_TIM_CNT(TIM_WATCHDOG) = 0x0000;
}

#endif  /* CONFIG_WATCHDOG_HELP */
