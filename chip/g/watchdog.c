/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Watchdog driver */

#include "common.h"
#include "hooks.h"
#include "registers.h"
#include "task.h"
#include "util.h"
#include "watchdog.h"

/* magic value to unlock the watchdog registers */
#define WATCHDOG_MAGIC_WORD  0x1ACCE551

/* Watchdog expiration : assume 30 Mhz clock for now */
#define WATCHDOG_PERIOD (CONFIG_WATCHDOG_PERIOD_MS * (30000000 / 1000))

/* Warning interrupt at the middle of the watchdog period */
void IRQ_HANDLER(GC_IRQNUM_WATCHDOG0_WDOGINT)(void) __attribute__((naked));
void IRQ_HANDLER(GC_IRQNUM_WATCHDOG0_WDOGINT)(void)
{
	/* Naked call so we can extract raw LR and SP */
	asm volatile("mov r0, lr\n"
		     "mov r1, sp\n"
		     /* Must push registers in pairs to keep 64-bit aligned
		      * stack for ARM EABI.  This also conveninently saves
		      * R0=LR so we can pass it to task_resched_if_needed. */
		     "push {r0, lr}\n"
		     "bl watchdog_trace\n"
		      /* Do NOT reset the watchdog interrupt here; it will
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
			: : [irq] "i" (GC_IRQNUM_WATCHDOG0_WDOGINT));
}
const struct irq_priority IRQ_PRIORITY(GC_IRQNUM_WATCHDOG0_WDOGINT)
	__attribute__((section(".rodata.irqprio")))
		= {GC_IRQNUM_WATCHDOG0_WDOGINT, 0};
	/* put the watchdog at the highest priority */

void watchdog_reload(void)
{
	uint32_t status = GR_WATCHDOG_RIS;

	/* Unlock watchdog registers */
	GR_WATCHDOG_LOCK = WATCHDOG_MAGIC_WORD;

	/* As we reboot only on the second timeout, if we have already reached
	 * the first timeout we need to reset the interrupt bit. */
	if (status) {
		GR_WATCHDOG_ICR = status;
		/* That doesn't seem to unpend the watchdog interrupt (even if
		 * we do dummy writes to force the write to be committed), so
		 * explicitly unpend the interrupt before re-enabling it. */
		task_clear_pending_irq(GC_IRQNUM_WATCHDOG0_WDOGINT);
		task_enable_irq(GC_IRQNUM_WATCHDOG0_WDOGINT);
	}

	/* Reload the watchdog counter */
	GR_WATCHDOG_LOAD = WATCHDOG_PERIOD;

	/* Re-lock watchdog registers */
	GR_WATCHDOG_LOCK = 0xdeaddead;
}
DECLARE_HOOK(HOOK_TICK, watchdog_reload, HOOK_PRIO_DEFAULT);

int watchdog_init(void)
{
	/* Enable clocks */
	REG_WRITE_MLV(GR_PMU_PERICLKSET0,
		      GC_PMU_PERICLKSET0_DWATCHDOG0_MASK,
		      GC_PMU_PERICLKSET0_DWATCHDOG0_LSB, 1);

	/* Unlock watchdog registers */
	GR_WATCHDOG_LOCK = WATCHDOG_MAGIC_WORD;

	/* Reload the watchdog counter */
	GR_WATCHDOG_LOAD = WATCHDOG_PERIOD;

	/* Reset after 2 time-out : activate both interrupt and reset. */
	GR_WATCHDOG_CTL = 0x3;

	/* Reset watchdog interrupt bits */
	GR_WATCHDOG_ICR = GR_WATCHDOG_RIS;

	/* Lock watchdog registers against unintended accesses */
	GR_WATCHDOG_LOCK = 0xdeaddead;

	/* Enable watchdog interrupt */
	task_enable_irq(GC_IRQNUM_WATCHDOG0_WDOGINT);

	return EC_SUCCESS;
}
