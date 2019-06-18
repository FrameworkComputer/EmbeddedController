/* Copyright 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Watchdog driver */

#include "hooks.h"
#include "registers.h"
#include "task.h"
#include "watchdog.h"

void watchdog_reload(void)
{
	MEC1322_WDG_KICK = 1;

#ifdef CONFIG_WATCHDOG_HELP
	/* Reload the auxiliary timer */
	MEC1322_TMR16_CTL(0) &= ~BIT(5);
	MEC1322_TMR16_CNT(0) = CONFIG_AUX_TIMER_PERIOD_MS;
	MEC1322_TMR16_CTL(0) |= BIT(5);
#endif
}
DECLARE_HOOK(HOOK_TICK, watchdog_reload, HOOK_PRIO_DEFAULT);

int watchdog_init(void)
{
#ifdef CONFIG_WATCHDOG_HELP
	uint32_t val;

	/*
	 * Watchdog does not warn us before expiring. Let's use a 16-bit
	 * timer as an auxiliary timer.
	 */

	/* Stop the auxiliary timer if it's running */
	MEC1322_TMR16_CTL(0) &= ~BIT(5);

	/* Enable auxiliary timer */
	MEC1322_TMR16_CTL(0) |= BIT(0);

	val = MEC1322_TMR16_CTL(0);

	/* Pre-scale = 48000 -> 1kHz -> Period = 1ms */
	val = (val & 0xffff) | (47999 << 16);

	/* No auto restart */
	val &= ~BIT(3);

	/* Count down */
	val &= ~BIT(2);

	MEC1322_TMR16_CTL(0) = val;

	/* Enable interrupt from auxiliary timer */
	MEC1322_TMR16_IEN(0) |= 1;
	task_enable_irq(MEC1322_IRQ_TIMER16_0);
	MEC1322_INT_ENABLE(23) |= BIT(0);
	MEC1322_INT_BLK_EN |= BIT(23);

	/* Load and start the auxiliary timer */
	MEC1322_TMR16_CNT(0) = CONFIG_AUX_TIMER_PERIOD_MS;
	MEC1322_TMR16_CNT(0) |= BIT(5);
#endif

	/* Set timeout. It takes 1007us to decrement WDG_CNT by 1. */
	MEC1322_WDG_LOAD = CONFIG_WATCHDOG_PERIOD_MS * 1000 / 1007;

	/* Start watchdog */
	MEC1322_WDG_CTL |= 1;

	return EC_SUCCESS;
}

#ifdef CONFIG_WATCHDOG_HELP
void __keep watchdog_check(uint32_t excep_lr, uint32_t excep_sp)
{
	/* Clear status */
	MEC1322_TMR16_STS(0) |= 1;

	watchdog_trace(excep_lr, excep_sp);
}

void IRQ_HANDLER(MEC1322_IRQ_TIMER16_0)(void) __attribute__((naked));
void IRQ_HANDLER(MEC1322_IRQ_TIMER16_0)(void)
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
const struct irq_priority __keep IRQ_PRIORITY(MEC1322_IRQ_TIMER16_0)
	__attribute__((section(".rodata.irqprio")))
		= {MEC1322_IRQ_TIMER16_0, 0}; /* put the watchdog at the
						 highest priority */
#endif
