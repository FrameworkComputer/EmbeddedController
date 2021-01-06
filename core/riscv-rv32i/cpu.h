/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Registers map and definitions for RISC-V cores
 */

#ifndef __CROS_EC_CPU_H
#define __CROS_EC_CPU_H

/*
 * This is the space required by both __irq_isr and __switch_task to store all
 * of the caller and callee registers for each task context before switching.
 */
#ifdef CONFIG_FPU
/* additional space to save FP registers (fcsr, ft0-11, fa0-7, fs0-11) */
#define TASK_SCRATCHPAD_SIZE (62)
#else
#define TASK_SCRATCHPAD_SIZE (29)
#endif

#ifndef __ASSEMBLER__
#include <stdint.h>

/* write Exception Program Counter register */
static inline void set_mepc(uint32_t val)
{
	asm volatile ("csrw mepc, %0" : : "r"(val));
}

/* read Exception Program Counter register */
static inline uint32_t get_mepc(void)
{
	uint32_t ret;

	asm volatile ("csrr %0, mepc" : "=r"(ret));
	return ret;
}

/* read Trap cause register */
static inline uint32_t get_mcause(void)
{
	uint32_t ret;

	asm volatile ("csrr %0, mcause" : "=r"(ret));
	return ret;
}

/* Generic CPU core initialization */
void cpu_init(void);
extern uint32_t ec_reset_lp;
extern uint32_t ira;
#endif

#endif /* __CROS_EC_CPU_H */
