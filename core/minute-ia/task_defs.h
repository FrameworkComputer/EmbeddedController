/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_TASK_DEFS_H
#define __CROS_EC_TASK_DEFS_H

#ifdef CONFIG_FPU
#define FPU_CTX_SZ		108  /* 28 bytes header + 80 bytes registers */
#define USE_FPU_OFFSET		20   /* offsetof(task_, use_fpu */
#define FPU_CTX_OFFSET		24   /* offsetof(task_, fp_ctx) */

/*
 * defines for inline asm
 */
#ifndef __ASSEMBLER__
#include "common.h"

#define USE_FPU_OFFSET_STR	STRINGIFY(USE_FPU_OFFSET)	/* "20" */
#define FPU_CTX_OFFSET_STR	STRINGIFY(FPU_CTX_OFFSET)	/* "24" */

asm (".equ USE_FPU_OFFSET, "USE_FPU_OFFSET_STR);
asm (".equ FPU_CTX_OFFSET, "FPU_CTX_OFFSET_STR);

#endif
#endif /* CONFIG_FPU */

#ifndef __ASSEMBLER__
typedef union {
	struct {
		/*
		 * Note that sp must be the first element in the task struct
		 * for __switchto() to work.
		 */
		uint32_t sp;	/* Saved stack pointer for context switch */
		uint32_t events;	/* Bitmaps of received events */
		uint64_t runtime;	/* Time spent in task */
		uint32_t *stack;	/* Start of stack */
#ifdef CONFIG_FPU
		uint32_t use_fpu;	/* set if task uses FPU */
		uint8_t fp_ctx[FPU_CTX_SZ]; /* x87 FPU context */
#endif
	};
} task_;

int __task_start(int *start_called);
void __switchto(void);
void sw_irq_handler(void);

/* Only the IF bit is set so tasks start with interrupts enabled. */
#define INITIAL_EFLAGS		(0x200UL)

/* LAPIC ICR bit fields
 *	7:0	- vector
 *	10:8	- Delivery mode (0 = fixed)
 *	11	- Destination mode (0 = physical)
 *	12	- Delivery status (0 = Idle)
 *	14	- Level (1 = assert)
 *	15	- Trigger mode (0 = edge)
 *	20:18	- Destination (1 = self)
 */
#define LAPIC_ICR_BITS		0x44000

#endif /* __ASSEMBLER__ */
#endif /* __CROS_EC_TASK_DEFS_H */
