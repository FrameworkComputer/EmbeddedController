/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* tiny substitute of the runtime layer */

#include "common.h"
#include "cpu.h"
#include "debug.h"
#include "registers.h"
#include "task.h"
#include "timer.h"
#include "util.h"

volatile uint32_t last_event;

timestamp_t get_time(void)
{
	timestamp_t t;

	t.le.lo = STM32_TIM32_CNT(2);
	t.le.hi = 0;
	return t;
}

void udelay(unsigned us)
{
	unsigned t0 = STM32_TIM32_CNT(2);
	while ((STM32_TIM32_CNT(2) - t0) < us)
		;
}

void task_enable_irq(int irq)
{
	CPU_NVIC_EN(0) = 1 << irq;
}

void task_disable_irq(int irq)
{
	CPU_NVIC_DIS(0) = 1 << irq;
}

void task_clear_pending_irq(int irq)
{
	CPU_NVIC_UNPEND(0) = 1 << irq;
}

uint32_t task_set_event(task_id_t tskid, uint32_t event, int wait)
{
	last_event = event;

	return 0;
}

void tim2_interrupt(void)
{
	STM32_TIM_DIER(2) = 0; /* disable match interrupt */
	task_clear_pending_irq(STM32_IRQ_TIM2);
	last_event = TASK_EVENT_TIMER;
}
DECLARE_IRQ(STM32_IRQ_TIM2, tim2_interrupt, 1);

uint32_t task_wait_event(int timeout_us)
{
	uint32_t evt;

	asm volatile("cpsid i");
	/* the event already happened */
	if (last_event || !timeout_us) {
		evt = last_event;
		last_event = 0;

		asm volatile("cpsie i ; isb");
		return evt;
	}

	/* set timeout on timer */
	if (timeout_us > 0) {
		STM32_TIM32_CCR1(2) = STM32_TIM32_CNT(2) + timeout_us;
		STM32_TIM_SR(2) = 0; /* clear match flag */
		STM32_TIM_DIER(2) = 2; /*  match interrupt */
	}

	/* sleep until next interrupt */
	asm volatile("wfi");

	STM32_TIM_DIER(2) = 0; /* disable match interrupt */
	asm volatile("cpsie i ; isb");

	/* note: interrupt that woke us up will run here */

	evt = last_event;
	last_event = 0;
	return evt;
}

void cpu_reset(void)
{
	/* Disable interrupts */
	asm volatile("cpsid i");
	/* reboot the CPU */
	CPU_NVIC_APINT = 0x05fa0004;
	/* Spin and wait for reboot; should never return */
	while (1)
		;
}

/**
 * Default exception handler, which reports a panic.
 *
 * Declare this as a naked call so we can extract the real LR and SP.
 */
void exception_panic(void) __attribute__((naked));
void exception_panic(void)
{
	asm volatile(
#ifdef CONFIG_DEBUG_PRINTF
		"mov r0, %0\n"
		"mov r3, sp\n"
		"ldr r1, [r3, #6*4]\n" /* retrieve exception PC */
		"ldr r2, [r3, #5*4]\n" /* retrieve exception LR */
		"bl debug_printf\n"
#endif
		"b cpu_reset\n"
	: : "r"("PANIC PC=%08x LR=%08x\n\n"));
}

void panic_reboot(void)
{ /* for div / 0 */
	debug_printf("DIV0 PANIC\n\n");
	cpu_reset();
}

/* --- stubs --- */
void __hw_timer_enable_clock(int n, int enable)
{ /* Done in hardware init */ }

void usleep(unsigned us)
{ /* Used only as a workaround */ }
