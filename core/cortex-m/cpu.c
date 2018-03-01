/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Set up the Cortex-M core
 */

#include "common.h"
#include "cpu.h"
#include "hooks.h"

void cpu_init(void)
{
	/* Catch divide by 0 and unaligned access */
	CPU_NVIC_CCR |= CPU_NVIC_CCR_DIV_0_TRAP | CPU_NVIC_CCR_UNALIGN_TRAP;

	/* Enable reporting of memory faults, bus faults and usage faults */
	CPU_NVIC_SHCSR |= CPU_NVIC_SHCSR_MEMFAULTENA |
		CPU_NVIC_SHCSR_BUSFAULTENA | CPU_NVIC_SHCSR_USGFAULTENA;
}

#ifdef CONFIG_ARMV7M_CACHE
static void cpu_invalidate_icache(void)
{
	/*
	 * Invalidates the entire instruction cache to the point of
	 * unification.
	 */
	CPU_SCB_ICIALLU = 0;
	asm volatile("dsb; isb");
}

void cpu_enable_icache(void)
{
	/* Check whether the I-cache is already enabled */
	if (!(CPU_NVIC_CCR & CPU_NVIC_CCR_ICACHE)) {
		/* Invalidate the I-cache first */
		cpu_invalidate_icache();
		/* Turn on the caching */
		CPU_NVIC_CCR |= CPU_NVIC_CCR_ICACHE;
		asm volatile("dsb; isb");
	}
}

static void cpu_sysjump_cache(void)
{
	/*
	 * Disable the I-cache
	 * so we will invalidate it after the sysjump if needed
	 * (e.g after a flash update).
	 */
	CPU_NVIC_CCR &= ~CPU_NVIC_CCR_ICACHE;
	asm volatile("dsb; isb");
}
DECLARE_HOOK(HOOK_SYSJUMP, cpu_sysjump_cache, HOOK_PRIO_LAST);
#endif /* CONFIG_ARMV7M_CACHE */
