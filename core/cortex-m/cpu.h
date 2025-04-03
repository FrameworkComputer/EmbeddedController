/* Copyright 2012 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Registers map and definitions for Cortex-MLM4x processor
 */

#ifndef __CROS_EC_CPU_H
#define __CROS_EC_CPU_H

#include "compile_time_macros.h"
#include "debug.h"

#include <stdint.h>

/* Macro to access 32-bit registers */
#define CPUREG(addr) (*(volatile uint32_t *)(addr))

#define CPU_NVIC_ST_CTRL CPUREG(0xE000E010)
#define ST_ENABLE BIT(0)
#define ST_TICKINT BIT(1)
#define ST_CLKSOURCE BIT(2)
#define ST_COUNTFLAG BIT(16)

/* Nested Vectored Interrupt Controller */
#define CPU_NVIC_EN(x) CPUREG(0xe000e100 + 4 * (x))
#define CPU_NVIC_DIS(x) CPUREG(0xe000e180 + 4 * (x))
#define CPU_NVIC_UNPEND(x) CPUREG(0xe000e280 + 4 * (x))
#define CPU_NVIC_PRI(x) CPUREG(0xe000e400 + 4 * (x))
/* SCB AIRCR : Application interrupt and reset control register */
#define CPU_NVIC_APINT CPUREG(0xe000ed0c)
#define CPU_NVIC_APINT_SYSRST BIT(2) /* System reset request */
#define CPU_NVIC_APINT_PRIOGRP (BIT(8) | BIT(9) | BIT(10))
#define CPU_NVIC_APINT_ENDIAN BIT(15) /* Endianness */
#define CPU_NVIC_APINT_KEY_RD (0xFA05U << 16)
#define CPU_NVIC_APINT_KEY_WR (0x05FAU << 16)
/* NVIC STIR : Software Trigger Interrupt Register */
#define CPU_NVIC_SWTRIG CPUREG(0xe000ef00)
/* SCB SCR : System Control Register */
#define CPU_SCB_SYSCTRL CPUREG(0xe000ed10)

#define CPU_NVIC_CCR CPUREG(0xe000ed14)
#define CPU_NVIC_SHCSR CPUREG(0xe000ed24)
#define CPU_NVIC_CFSR CPUREG(0xe000ed28)
#define CPU_NVIC_HFSR CPUREG(0xe000ed2c)
#define CPU_NVIC_DFSR CPUREG(0xe000ed30)
#define CPU_NVIC_MFAR CPUREG(0xe000ed34)
#define CPU_NVIC_BFAR CPUREG(0xe000ed38)

enum {
	CPU_NVIC_CFSR_BFARVALID = BIT(15),
	CPU_NVIC_CFSR_MFARVALID = BIT(7),

	CPU_NVIC_CCR_ICACHE = BIT(17),
	CPU_NVIC_CCR_DCACHE = BIT(16),
	CPU_NVIC_CCR_DIV_0_TRAP = BIT(4),
	CPU_NVIC_CCR_UNALIGN_TRAP = BIT(3),

	CPU_NVIC_HFSR_DEBUGEVT = 1UL << 31,
	CPU_NVIC_HFSR_FORCED = BIT(30),
	CPU_NVIC_HFSR_VECTTBL = BIT(1),

	CPU_NVIC_SHCSR_MEMFAULTENA = BIT(16),
	CPU_NVIC_SHCSR_BUSFAULTENA = BIT(17),
	CPU_NVIC_SHCSR_USGFAULTENA = BIT(18),
};

/* System Control Block: cache registers */
#define CPU_SCB_CCSIDR CPUREG(0xe000ed80)
#define CPU_SCB_CCSELR CPUREG(0xe000ed84)
#define CPU_SCB_ICIALLU CPUREG(0xe000ef50)
#define CPU_SCB_DCISW CPUREG(0xe000ef60)
#define CPU_SCB_DCCISW CPUREG(0xe000ef74)

/* Floating Point Context Address Register */
#define CPU_FPU_FPCAR CPUREG(0xe000ef38)

/*
 * As defined by Armv7-M Reference Manual B1.5.7 "Context state stacking on
 * exception entry with the FP extension" the structure of the FPU state is:
 * s0, s1, ..., s14, s15, fpscr.
 */
#define FPU_IDX_REG_FPSCR 16
#define FPU_FPSCR_IOC BIT(0) /* Invalid operation */
#define FPU_FPSCR_DZC BIT(1) /* Division by zero */
#define FPU_FPSCR_OFC BIT(2) /* Overflow */
#define FPU_FPSCR_UFC BIT(3) /* Underflow */
#define FPU_FPSCR_IXC BIT(4) /* Inexact */
#define FPU_FPSCR_IDC BIT(7) /* Input denormal */
#define FPU_FPSCR_EXC_FLAGS                                              \
	(FPU_FPSCR_IOC | FPU_FPSCR_DZC | FPU_FPSCR_OFC | FPU_FPSCR_UFC | \
	 FPU_FPSCR_IXC | FPU_FPSCR_IDC)

/* Bitfield values for EXC_RETURN. */
#define EXC_RETURN_ES_MASK BIT(0)
#define EXC_RETURN_ES_NON_SECURE 0
#define EXC_RETURN_ES_SECURE BIT(0)
#define EXC_RETURN_SPSEL_MASK BIT(2)
#define EXC_RETURN_SPSEL_MSP 0
#define EXC_RETURN_SPSEL_PSP BIT(2)
#define EXC_RETURN_MODE_MASK BIT(3)
#define EXC_RETURN_MODE_HANDLER 0
#define EXC_RETURN_MODE_THREAD BIT(3)
#define EXC_RETURN_FTYPE_MASK BIT(4)
#define EXC_RETURN_FTYPE_ON 0
#define EXC_RETURN_FTYPE_OFF BIT(4)
#define EXC_RETURN_DCRS_MASK BIT(5)
#define EXC_RETURN_DCRS_OFF 0
#define EXC_RETURN_DCRS_ON BIT(5)
#define EXC_RETURN_S_MASK BIT(6)
#define EXC_RETURN_S_NON_SECURE 0
#define EXC_RETURN_S_SECURE BIT(6)

/* Set up the cpu to detect faults */
void cpu_init(void);
/* Enable the CPU I-cache and D-cache if they are not already enabled */
void cpu_enable_caches(void);
/* Disable the CPU I-cache and D-cache */
void cpu_disable_caches(void);
/* Invalidate the D-cache */
void cpu_invalidate_dcache(void);
/* Clean and Invalidate the D-cache to the Point of Coherency */
void cpu_clean_invalidate_dcache(void);

/* Invalidate a single range of the D-cache */
void cpu_invalidate_dcache_range(uintptr_t base, unsigned int length);
/* Clean and Invalidate a single range of the D-cache */
void cpu_clean_invalidate_dcache_range(uintptr_t base, unsigned int length);

/* Return to specified function from exception handler using main stack. */
void cpu_return_from_exception_msp(void (*func)(void));
/* Return to specified function from exception handler using process stack. */
void cpu_return_from_exception_psp(void (*func)(void));

/* Set the priority of the given IRQ in the NVIC (0 is highest). */
static inline void cpu_set_interrupt_priority(uint8_t irq, uint8_t priority)
{
	const uint32_t prio_shift = irq % 4 * 8 + 5;

	if (priority > 7)
		priority = 7;

	CPU_NVIC_PRI(irq / 4) = (CPU_NVIC_PRI(irq / 4) & ~(7 << prio_shift)) |
				(priority << prio_shift);
}

static inline void cpu_enter_suspend_mode(void)
{
	/* Preserve debug sessions by not suspending when connected */
	if (!debugger_is_connected()) {
		asm("wfi");
	}
}

/*
 * Returns true if the exception frame was created on the main stack,
 * false if the frame was created on the process stack.
 *
 * The least significant 4 bits of the exception LR determine
 * the exception stack and exception context.
 * - 0xd - CPU was in Thread Mode and PSP was used
 * - 0x9 - CPU was in Thread Mode and MSP was used
 * - 0x1 - CPU was in Handler Mode and MSP was used
 *
 * See B1.5.8 "Exception return behavior" of ARM DDI for details.
 */
static inline bool is_frame_in_handler_stack(const uint32_t exc_return)
{
#ifdef CONFIG_FPU
	return exc_return == 0xfffffff1 || exc_return == 0xfffffff9 ||
	       exc_return == 0xffffffe1 || exc_return == 0xffffffe9;
#else
	return exc_return == 0xfffffff1 || exc_return == 0xfffffff9;
#endif /* CONFIG_FPU */
}

/*
 * Returns true if the exception occurred in handler mode,
 * false if exception occurred in process mode.
 *
 * The least significant 4 bits of the exception LR determine
 * the exception stack and exception context.
 * - 0xd - CPU was in Thread Mode and PSP was used
 * - 0x9 - CPU was in Thread Mode and MSP was used
 * - 0x1 - CPU was in Handler Mode and MSP was used
 *
 * See B1.5.8 "Exception return behavior" of ARM DDI for details.
 */
static inline bool is_exception_from_handler_mode(const uint32_t exc_return)
{
#ifdef CONFIG_FPU
	return exc_return == 0xfffffff1 || exc_return == 0xffffffe1;
#else
	return exc_return == 0xfffffff1;
#endif /* CONFIG_FPU */
}

#endif /* __CROS_EC_CPU_H */
