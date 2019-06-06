/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Helper to declare IRQ handling routines */

#ifndef __CROS_EC_IRQ_HANDLER_H
#define __CROS_EC_IRQ_HANDLER_H

#include "registers.h"
#include "task.h"
#include "task_defs.h"

asm (".include \"core/minute-ia/irq_handler_common.S\"");

/* Helper macros to build the IRQ handler and priority struct names */
#define IRQ_HANDLER(irqname) CONCAT3(_irq_, irqname, _handler)
#define IRQ_PRIORITY(irqname) CONCAT2(prio_, irqname)
/*
 * Macro to connect the interrupt handler "routine" to the irq number "irq" and
 * ensure it is enabled in the interrupt controller with the right priority.
 *
 * Note: No 'naked' function support for x86, so function is implemented within
 * __asm__
 * Note: currently we don't allow nested irq handling
 */
#define DECLARE_IRQ(irq, routine) DECLARE_IRQ_(irq, routine, irq + 32 + 10)
/*
 * Each irq has a irq_data structure placed in .rodata.irqs section,
 * to be used for dynamically setting up interrupt gates
 */
#define DECLARE_IRQ_(irq_, routine_, vector)				\
	void __keep routine_(void);					\
	void IRQ_HANDLER(irq_)(void);					\
	__asm__ (".section .rodata.irqs\n");				\
	const struct irq_def __keep CONCAT4(__irq_, irq_, _, routine_)	\
	__attribute__((section(".rodata.irqs"))) = {			\
		.irq = irq_,						\
		.routine = routine_,					\
		.handler = IRQ_HANDLER(irq_)				\
	};								\
	__asm__ (							\
		".section .text._irq_" #irq_ "_handler\n"		\
		"_irq_" #irq_ "_handler:\n"				\
		"pusha\n"						\
		ASM_LOCK_PREFIX "addl  $1, __in_isr\n"			\
		"irq_handler_common $0 $0 $" #irq_ "\n"			\
		"movl $"#vector ", " STRINGIFY(IOAPIC_EOI_REG_ADDR) "\n" \
		"movl $0x00, " STRINGIFY(LAPIC_EOI_REG_ADDR) "\n"	\
		ASM_LOCK_PREFIX "subl  $1, __in_isr\n"			\
		"popa\n"						\
		"iret\n"						\
	)
#endif  /* __CROS_EC_IRQ_HANDLER_H */
