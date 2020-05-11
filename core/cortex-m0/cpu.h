/* Copyright 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Registers map and definitions for Cortex-M0 processor
 */

#ifndef __CROS_EC_CPU_H
#define __CROS_EC_CPU_H

#include <stdint.h>
#include "compile_time_macros.h"

/* Macro to access 32-bit registers */
#define CPUREG(addr) (*(volatile uint32_t*)(addr))

/* Nested Vectored Interrupt Controller */
#define CPU_NVIC_EN(x)         CPUREG(0xe000e100)
#define CPU_NVIC_DIS(x)        CPUREG(0xe000e180)
#define CPU_NVIC_UNPEND(x)     CPUREG(0xe000e280)
#define CPU_NVIC_ISPR(x)       CPUREG(0xe000e200)
#define CPU_NVIC_PRI(x)        CPUREG(0xe000e400 + 4 * (x))

/* System Control Block */
#define CPU_SCB_ICSR           CPUREG(0xe000ed04)

/* SCB AIRCR : Application interrupt and reset control register */
#define CPU_NVIC_APINT         CPUREG(0xe000ed0c)
#define  CPU_NVIC_APINT_SYSRST  BIT(2)            /* System reset request */
#define  CPU_NVIC_APINT_ENDIAN  BIT(15)           /* Endianness */
#define  CPU_NVIC_APINT_KEY_RD  (0U)
#define  CPU_NVIC_APINT_KEY_WR  (0x05FAU << 16)
/* SCB SCR : System Control Register */
#define CPU_SCB_SYSCTRL        CPUREG(0xe000ed10)
#define CPU_NVIC_CCR           CPUREG(0xe000ed14)
#define CPU_NVIC_SHCSR2        CPUREG(0xe000ed1c)
#define CPU_NVIC_SHCSR3        CPUREG(0xe000ed20)

#define CPU_NVIC_CCR_UNALIGN_TRAP BIT(3)

/* Set up the cpu to detect faults */
void cpu_init(void);

/* Set the priority of the given IRQ in the NVIC (0 is highest). */
static inline void cpu_set_interrupt_priority(uint8_t irq, uint8_t priority)
{
	const uint32_t prio_shift = irq % 4 * 8 + 6;

	if (priority > 3)
		priority = 3;

	CPU_NVIC_PRI(irq / 4) =
		(CPU_NVIC_PRI(irq / 4) &
		 ~(3 << prio_shift)) |
		(priority << prio_shift);
}

#endif /* __CROS_EC_CPU_H */
