/* Copyright 2014 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Registers map and definitions for Cortex-M0 processor
 */

#ifndef __CROS_EC_CPU_H
#define __CROS_EC_CPU_H

#include "compile_time_macros.h"
#include "debug.h"

#include <stdint.h>

/* Macro to access 32-bit registers */
#define CPUREG(addr) (*(volatile uint32_t *)(addr))

/* Nested Vectored Interrupt Controller */
#define CPU_NVIC_EN(x) CPUREG(0xe000e100)
#define CPU_NVIC_DIS(x) CPUREG(0xe000e180)
#define CPU_NVIC_UNPEND(x) CPUREG(0xe000e280)
#define CPU_NVIC_ISPR(x) CPUREG(0xe000e200)
#define CPU_NVIC_PRI(x) CPUREG(0xe000e400 + 4 * (x))

/* System Control Block */
#define CPU_SCB_ICSR CPUREG(0xe000ed04)

/* SCB AIRCR : Application interrupt and reset control register */
#define CPU_NVIC_APINT CPUREG(0xe000ed0c)
#define CPU_NVIC_APINT_SYSRST BIT(2) /* System reset request */
#define CPU_NVIC_APINT_ENDIAN BIT(15) /* Endianness */
#define CPU_NVIC_APINT_KEY_RD (0U)
#define CPU_NVIC_APINT_KEY_WR (0x05FAU << 16)
/* SCB SCR : System Control Register */
#define CPU_SCB_SYSCTRL CPUREG(0xe000ed10)
#define CPU_NVIC_CCR CPUREG(0xe000ed14)
#define CPU_NVIC_SHCSR2 CPUREG(0xe000ed1c)
#define CPU_NVIC_SHCSR3 CPUREG(0xe000ed20)

#define CPU_NVIC_CCR_UNALIGN_TRAP BIT(3)

/* Bitfield values for EXC_RETURN. */
#define EXC_RETURN_SPSEL_MASK BIT(2)
#define EXC_RETURN_SPSEL_MSP 0
#define EXC_RETURN_SPSEL_PSP BIT(2)
#define EXC_RETURN_MODE_MASK BIT(3)
#define EXC_RETURN_MODE_HANDLER 0
#define EXC_RETURN_MODE_THREAD BIT(3)

/* Set up the cpu to detect faults */
void cpu_init(void);

/* Set the priority of the given IRQ in the NVIC (0 is highest). */
static inline void cpu_set_interrupt_priority(uint8_t irq, uint8_t priority)
{
	const uint32_t prio_shift = irq % 4 * 8 + 6;

	if (priority > 3)
		priority = 3;

	CPU_NVIC_PRI(irq / 4) = (CPU_NVIC_PRI(irq / 4) & ~(3 << prio_shift)) |
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
 * See B1.5.8 "Exception return behavior" of ARM DDI 0403D for details.
 */
static inline bool is_frame_in_handler_stack(const uint32_t exc_return)
{
	return exc_return == 0xfffffff1 || exc_return == 0xfffffff9;
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
	return exc_return == 0xfffffff1;
}

#endif /* __CROS_EC_CPU_H */
