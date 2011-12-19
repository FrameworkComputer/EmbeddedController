/* Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Watchdog driver */

#include <stdint.h>

#include "board.h"
#include "common.h"
#include "config.h"
#include "registers.h"
#include "task.h"
#include "uart.h"
#include "util.h"

/*
 * We use watchdog 0 which is clocked on the system clock
 * to avoid the penalty cycles on each write access
 */

/* magic value to unlock the watchdog registers */
#define LM4_WATCHDOG_MAGIC_WORD  0x1ACCE551

/* watchdog counter initial value */
static uint32_t watchdog_period;

/* console debug command prototypes */
int command_task_info(int argc, char **argv);
int command_timer_info(int argc, char **argv);

/**
 * watchdog debug trace.
 *
 * It is triggered if the watchdog has not been reloaded after 1x the timeout
 * period, after 2x the period an hardware reset is triggering.
 */
void watchdog_trace(uint32_t excep_lr, uint32_t excep_sp)
{
	uint32_t psp;
	uint32_t *stack;

	/* we do NOT reset the watchdog interrupt here, it will be done in
	 * watchdog_reload() or fire the reset
	 * instead de-activate the interrupt in the NVIC :
	 * so, we will get the trace only once
	 */
	task_disable_irq(LM4_IRQ_WATCHDOG);

	asm("mrs %0, psp":"=r"(psp));
	if ((excep_lr & 0xf) == 1) {
		/* we were already in exception context */
		stack = (uint32_t *)excep_sp;
	} else {
		/* we were in task context */
		stack = (uint32_t *)psp;
	}

	uart_printf("### WATCHDOG PC=%08x / LR=%08x / pSP=%08x ###\n",
	            stack[6], stack[5], psp);
	/* ensure this debug message is always flushed to the UART */
	uart_emergency_flush();
	/* if we are blocked in a high priority IT handler, the following
	 * debug messages might not appear but they are useless in that
	 * situation.
	 */
	command_task_info(0, NULL);
	command_timer_info(0, NULL);
}

void irq_LM4_IRQ_WATCHDOG_handler(void) __attribute__((naked));
void irq_LM4_IRQ_WATCHDOG_handler(void)
{
	asm volatile("mov r0, lr\n"
	             "mov r1, sp\n"
	             "push {lr}\n"
	             "bl watchdog_trace\n"
	             "pop {lr}\n"
	             "mov r0, lr\n"
		     "b task_resched_if_needed\n");
}
const struct irq_priority prio_LM4_IRQ_WATCHDOG
	__attribute__((section(".rodata.irqprio")))
		= {LM4_IRQ_WATCHDOG, 0}; /* put the watchdog at the highest
					    priority */

void watchdog_reload(void)
{
	uint32_t status = LM4_WATCHDOG_RIS(0);

	/* unlock watchdog registers */
	LM4_WATCHDOG_LOCK(0) = LM4_WATCHDOG_MAGIC_WORD;

	/* As we reboot only on the second time-out,
	 * if we have already reached 1 time-out
	 * we need to reset the interrupt bit.
	 */
	if (status)
		LM4_WATCHDOG_ICR(0) = status;

	/* reload the watchdog counter */
	LM4_WATCHDOG_LOAD(0) = watchdog_period;

	/* re-lock watchdog registers */
	LM4_WATCHDOG_LOCK(0) = 0xdeaddead;
}

int watchdog_init(int period_ms)
{
	volatile uint32_t scratch  __attribute__((unused));

	/* Enable watchdog 0 clock */
	LM4_SYSTEM_RCGCWD |= 0x1;
	/* wait 3 clock cycles before using the module */
	scratch = LM4_SYSTEM_RCGCWD;

	/* set the time-out period */
	watchdog_period = period_ms * (CPU_CLOCK / 1000);
	LM4_WATCHDOG_LOAD(0) = watchdog_period;

	/* de-activate the watchdog when the JTAG stops the CPU */
	LM4_WATCHDOG_TEST(0) |= 1 << 8;

	/* reset after 2 time-out,
	 * activate the watchdog and lock the control register
	 */
	LM4_WATCHDOG_CTL(0) = 0x3;

	/* lock watchdog registers against unintended accesses */
	LM4_WATCHDOG_LOCK(0) = 0xdeaddead;

	return EC_SUCCESS;
}
