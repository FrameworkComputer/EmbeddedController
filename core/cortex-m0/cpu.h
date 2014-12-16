/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Registers map and definitions for Cortex-M0 processor
 */

#ifndef __CPU_H
#define __CPU_H

#include <stdint.h>

/* Macro to access 32-bit registers */
#define CPUREG(addr) (*(volatile uint32_t*)(addr))

/* Nested Vectored Interrupt Controller */
#define CPU_NVIC_EN(x)         CPUREG(0xe000e100)
#define CPU_NVIC_DIS(x)        CPUREG(0xe000e180)
#define CPU_NVIC_UNPEND(x)     CPUREG(0xe000e280)
#define CPU_NVIC_ISPR(x)       CPUREG(0xe000e200)
#define CPU_NVIC_PRI(x)        CPUREG(0xe000e400 + 4 * (x))

/* System Control Block */

/* SCB AIRCR : Application interrupt and reset control register */
#define CPU_NVIC_APINT         CPUREG(0xe000ed0c)
/* SCB SCR : System Control Register */
#define CPU_SCB_SYSCTRL        CPUREG(0xe000ed10)
#define CPU_NVIC_CCR           CPUREG(0xe000ed14)
#define CPU_NVIC_SHCSR2        CPUREG(0xe000ed1c)
#define CPU_NVIC_SHCSR3        CPUREG(0xe000ed20)

#define CPU_NVIC_CCR_UNALIGN_TRAP (1 << 3)

/* Set up the cpu to detect faults */
void cpu_init(void);

#endif /* __CPU_H */
