/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Watchdog driver */

#include "clock.h"
#include "common.h"
#include "console.h"
#include "hooks.h"
#include "registers.h"
#include "task.h"
#include "util.h"
#include "watchdog.h"

void IRQ_HANDLER(ROTOR_MCU_IRQ_WDT)(void) __attribute__((naked));
void IRQ_HANDLER(ROTOR_MCU_IRQ_WDT)(void)
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
		     "bl watchdog_trace\n"
		      /*
		       * Do NOT reset the watchdog interrupt here; it will
		       * be done in watchdog_reload(), or reset will be
		       * triggered if we don't call that by the next watchdog
		       * period.  Instead, de-activate the interrupt in the
		       * NVIC, so the watchdog trace will only be printed
		       * once.
		       */
		     "mov r0, %[irq]\n"
		     "bl task_disable_irq\n"
		     "pop {r0, lr}\n"
		     "b task_resched_if_needed\n"
			: : [irq] "i" (ROTOR_MCU_IRQ_WDT));
}
const struct irq_priority IRQ_PRIORITY(ROTOR_MCU_IRQ_WDT)
	__attribute__((section(".rodata.irqprio")))
		= {ROTOR_MCU_IRQ_WDT, 0}; /*
					   * put the watchdog at the highest
					   * priority.
					   */

void watchdog_reload(void)
{
	/* Kick the watchdog. */
	ROTOR_MCU_WDT_CRR = ROTOR_MCU_WDT_KICK;
}
DECLARE_HOOK(HOOK_TICK, watchdog_reload, HOOK_PRIO_DEFAULT);

int watchdog_init(void)
{
	int i;
	/*
	 * Set timeout period.
	 *
	 * The watchdog timer only allows you to set a timeout period that's a
	 * power of two.  So find the most significant bit.  The TOP field only
	 * takes a nibble which is then added to 16.  Unfortunately, there will
	 * be some error.
	 */
	i = __fls(CONFIG_WATCHDOG_PERIOD_MS * (clock_get_freq() / 1000)) - 16;
	if (i > 0)
		ROTOR_MCU_WDT_TORR = (i & 0x0f);
	else
		ROTOR_MCU_WDT_TORR = 0;

	/*
	 * Reset after 2 timeouts.  Reset pulse of 2 pclk cycles, and enable
	 * the WDT.
	 */
	ROTOR_MCU_WDT_CR = (5 << 2) | 0x3;

	/* Kick */
	watchdog_reload();

	/* Enable WDT interrupt. */
	task_enable_irq(ROTOR_MCU_IRQ_WDT);

	return EC_SUCCESS;
}
