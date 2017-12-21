/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Watchdog driver */

#include "hooks.h"
#include "registers.h"
#include "task.h"
#include "watchdog.h"
#include "tfdp_chip.h"

void watchdog_reload(void)
{
	MCHP_WDG_KICK = 1;

#ifdef CONFIG_WATCHDOG_HELP
	/* Reload the auxiliary timer */
	MCHP_TMR16_CTL(0) &= ~(1 << 5);
	MCHP_TMR16_CNT(0) = CONFIG_AUX_TIMER_PERIOD_MS;
#ifndef CONFIG_CHIPSET_DEBUG
	MCHP_TMR16_CTL(0) |= 1 << 5;
#endif
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

	/* Clear 16-bit basic timer 0 PCR sleep enable */
	MCHP_PCR_SLP_DIS_DEV(MCHP_PCR_BTMR16_0);

	/* Stop the auxiliary timer if it's running */
	MCHP_TMR16_CTL(0) &= ~(1 << 5);

	/* Enable auxiliary timer */
	MCHP_TMR16_CTL(0) |= 1 << 0;

	val = MCHP_TMR16_CTL(0);

	/* Pre-scale = 48000 -> 1kHz -> Period = 1ms */
	val = (val & 0xffff) | (47999 << 16);

	/* No auto restart */
	val &= ~(1 << 3);

	/* Count down */
	val &= ~(1 << 2);

	MCHP_TMR16_CTL(0) = val;

#ifndef CONFIG_CHIPSET_DEBUG
	/* Enable interrupt from auxiliary timer */
	MCHP_TMR16_IEN(0) |= 1;
	task_enable_irq(MCHP_IRQ_TIMER16_0);
	MCHP_INT_ENABLE(MCHP_TMR16_GIRQ) = MCHP_TMR16_GIRQ_BIT(0);

	/* Load and start the auxiliary timer */
	MCHP_TMR16_CNT(0) = CONFIG_AUX_TIMER_PERIOD_MS;
	MCHP_TMR16_CNT(0) |= 1 << 5;
#endif
#endif

	/* Clear WDT PCR sleep enable */
	MCHP_PCR_SLP_DIS_DEV(MCHP_PCR_WDT);

	/* Set timeout. It takes 1007us to decrement WDG_CNT by 1. */
	MCHP_WDG_LOAD = CONFIG_WATCHDOG_PERIOD_MS * 1000 / 1007;

	/* Start watchdog */
#ifdef CONFIG_CHIPSET_DEBUG
	/* debug, set stall and do not start */
	MCHP_WDG_CTL = (1 << 4); /* enable WDG stall on active JTAG */
#else
	MCHP_WDG_CTL |= 1;
#endif

	return EC_SUCCESS;
}

#ifdef CONFIG_WATCHDOG_HELP
void __keep watchdog_check(uint32_t excep_lr, uint32_t excep_sp)
{
	trace0(0, WDT, 0, "Watchdog check from 16-bit basic timer0 ISR");

	/* Clear status */
	MCHP_TMR16_STS(0) |= 1;
	/* clear aggregator status */
	MCHP_INT_SOURCE(MCHP_TMR16_GIRQ) = MCHP_TMR16_GIRQ_BIT(0);

	watchdog_trace(excep_lr, excep_sp);
}

void
IRQ_HANDLER(MCHP_IRQ_TIMER16_0)(void) __keep __attribute__((naked));
void IRQ_HANDLER(MCHP_IRQ_TIMER16_0)(void)
{
	/* Naked call so we can extract raw LR and SP */
	asm volatile("mov r0, lr\n"
		     "mov r1, sp\n"
		     /*
		      * Must push registers in pairs to keep 64-bit aligned
		      * stack for ARM EABI.  This also conveninently saves
		      * R0=LR so we can pass it to task_resched_if_needed.
		      */
		     "push {r0, lr}\n"
		     "bl watchdog_check\n"
		     "pop {r0, lr}\n"
		     "b task_resched_if_needed\n");
}

/*
 * Put the watchdog at the highest interrupt priority.
 */
const struct irq_priority __keep IRQ_PRIORITY(MEC1322_IRQ_TIMER16_0)
	__attribute__((section(".rodata.irqprio")))
		= {MCHP_IRQ_TIMER16_0, 0};
#endif
