/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Registers map and defintions for Cortex-MLM4x processor
 */

#ifndef __CPU_H
#define __CPU_H

#include <stdint.h>

/* Macro to access 32-bit registers */
#define CPUREG(addr) (*(volatile uint32_t*)(addr))

/* Nested Vectored Interrupt Controller */
#define CPU_NVIC_EN(x)         CPUREG(0xe000e100 + 4 * (x))
#define CPU_NVIC_DIS(x)        CPUREG(0xe000e180 + 4 * (x))
#define CPU_NVIC_UNPEND(x)     CPUREG(0xe000e280 + 4 * (x))
#define CPU_NVIC_PRI(x)        CPUREG(0xe000e400 + 4 * (x))
#define CPU_NVIC_APINT         CPUREG(0xe000ed0c)
#define CPU_NVIC_SWTRIG        CPUREG(0xe000ef00)

#define CPU_SCB_SYSCTRL        CPUREG(0xe000ed10)

#endif /* __CPU_H */
