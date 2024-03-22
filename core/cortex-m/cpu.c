/* Copyright 2012 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Set up the Cortex-M core
 */

#include "common.h"
#include "cpu.h"
#include "hooks.h"

#define STACK_IDX_REG_LR 5
#define STACK_IDX_REG_PC 6
#define STACK_IDX_REG_PSR 7

void cpu_init(void)
{
	/* Catch divide by 0 */
	CPU_NVIC_CCR |= CPU_NVIC_CCR_DIV_0_TRAP;

	if (IS_ENABLED(CONFIG_ALLOW_UNALIGNED_ACCESS)) {
		/* Disable trapping on unaligned access */
		CPU_NVIC_CCR &= ~CPU_NVIC_CCR_UNALIGN_TRAP;
	} else {
		/* Catch unaligned access */
		CPU_NVIC_CCR |= CPU_NVIC_CCR_UNALIGN_TRAP;
	}

	/* Enable reporting of memory faults, bus faults and usage faults */
	CPU_NVIC_SHCSR |= CPU_NVIC_SHCSR_MEMFAULTENA |
			  CPU_NVIC_SHCSR_BUSFAULTENA |
			  CPU_NVIC_SHCSR_USGFAULTENA;
}

void cpu_return_from_exception_msp(void (*func)(void))
{
	uint32_t *msp;

	__asm__ volatile("mrs %0, msp" : "=r"(msp));

	msp[STACK_IDX_REG_LR] = 0; /* Will never return */
	msp[STACK_IDX_REG_PC] = (uint32_t)func; /* Return to this function */
	msp[STACK_IDX_REG_PSR] = (1 << 24); /* Just set thumb mode */

	/* Return from exception using main stack */
	__asm__ volatile("bx %0" : : "r"(0xFFFFFFF9));

	/* should not reach here */
	__builtin_unreachable();
}

void cpu_return_from_exception_psp(void (*func)(void))
{
	uint32_t *psp;

	__asm__ volatile("mrs %0, psp" : "=r"(psp));

	psp[STACK_IDX_REG_LR] = 0; /* Will never return */
	psp[STACK_IDX_REG_PC] = (uint32_t)func; /* Return to this function */
	psp[STACK_IDX_REG_PSR] = (1 << 24); /* Just set thumb mode */

	/* Return from exception using main stack */
	__asm__ volatile("bx %0" : : "r"(0xFFFFFFFD));

	/* should not reach here */
	__builtin_unreachable();
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

void cpu_enable_caches(void)
{
	/* Check whether the I-cache is already enabled */
	if (!(CPU_NVIC_CCR & CPU_NVIC_CCR_ICACHE)) {
		/* Invalidate the I-cache first */
		cpu_invalidate_icache();
		/* Turn on the caching */
		CPU_NVIC_CCR |= CPU_NVIC_CCR_ICACHE;
		asm volatile("dsb; isb");
	}
	/* Check whether the D-cache is already enabled */
	if (!(CPU_NVIC_CCR & CPU_NVIC_CCR_DCACHE)) {
		/* Invalidate the D-cache first */
		cpu_invalidate_dcache();
		/* Turn on the caching */
		CPU_NVIC_CCR |= CPU_NVIC_CCR_DCACHE;
		asm volatile("dsb; isb");
	}
}

void cpu_disable_caches(void)
{
	/*
	 * Disable the I-cache and the D-cache
	 * The I-cache will be invalidated after the reboot/sysjump if needed
	 * (e.g after a flash update).
	 */
	cpu_clean_invalidate_dcache();
	CPU_NVIC_CCR &= ~(CPU_NVIC_CCR_ICACHE | CPU_NVIC_CCR_DCACHE);
	asm volatile("dsb; isb");
}
DECLARE_HOOK(HOOK_SYSJUMP, cpu_disable_caches, HOOK_PRIO_LAST);
#endif /* CONFIG_ARMV7M_CACHE */
