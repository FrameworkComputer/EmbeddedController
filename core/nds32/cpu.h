/* Copyright 2013 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Registers map and definitions for Andes cores
 */

#ifndef __CROS_EC_CPU_H
#define __CROS_EC_CPU_H

/*
 * This is the space required by both irq_x_ and __switch_task to store all
 * of the caller and callee registers for each task context before switching.
 */
#define TASK_SCRATCHPAD_SIZE (18)

/* Process Status Word bits */
#define PSW_GIE BIT(0) /* Global Interrupt Enable */
#define PSW_INTL_SHIFT 1 /* Interrupt Stack Level */
#define PSW_INTL_MASK (0x3 << PSW_INTL_SHIFT)

#ifndef __ASSEMBLER__

#include <stdint.h>

/* write Process Status Word privileged register */
static inline void set_psw(uint32_t val)
{
	asm volatile("mtsr %0, $PSW" : : "r"(val));
}

/* read Process Status Word privileged register */
static inline uint32_t get_psw(void)
{
	uint32_t ret;
	asm volatile("mfsr %0, $PSW" : "=r"(ret));
	return ret;
}

/* write Interruption Program Counter privileged register */
static inline void set_ipc(uint32_t val)
{
	asm volatile("mtsr %0, $IPC" : : "r"(val));
}

/* read Interruption Program Counter privileged register */
static inline uint32_t get_ipc(void)
{
	uint32_t ret;
	asm volatile("mfsr %0, $IPC" : "=r"(ret));
	return ret;
}

/* read Interruption Type privileged register */
static inline uint32_t get_itype(void)
{
	uint32_t ret;
	asm volatile("mfsr %0, $ITYPE" : "=r"(ret));
	return ret;
}

static inline uint32_t get_interrupt_level(void)
{
	/* Get interrupt stack level, 0 | 1 | 2 */
	return (get_psw() & PSW_INTL_MASK) >> PSW_INTL_SHIFT;
}

/* Generic CPU core initialization */
void cpu_init(void);

extern uint32_t ilp;
extern uint32_t ec_reset_lp;

#endif /* !__ASSEMBLER__ */

#endif /* __CROS_EC_CPU_H */
