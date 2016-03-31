/* Copyright (c) 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Helper to declare IRQ handling routines */

#ifndef __CROS_EC_IRQ_HANDLER_H
#define __CROS_EC_IRQ_HANDLER_H

#include "registers.h"

struct irq_data {
	void (*routine)(void);
	int irq;
};

/* Helper macros to build the IRQ handler and priority struct names */
#define IRQ_HANDLER(irqname) CONCAT3(_irq_, irqname, _handler)
#define IRQ_PRIORITY(irqname) CONCAT2(prio_, irqname)
/*
 * Macro to connect the interrupt handler "routine" to the irq number "irq" and
 * ensure it is enabled in the interrupt controller with the right priority.
 *
 * Note: No 'naked' function support for x86, so function is implemented within
 * __asm__
 */
#define DECLARE_IRQ(irq, routine) DECLARE_IRQ_(irq, routine, irq + 32 + 10)
/* Each irq has a irq_data structure placed in .rodata.irqs section,
 * to be used for dynamically setting up interrupt gates */
#define DECLARE_IRQ_(irq, routine, vector)				\
	void __keep routine(void);					\
	void IRQ_HANDLER(irq)(void); 					\
	__asm__ (".section .rodata.irqs\n");				\
	const struct irq_data __keep CONCAT4(__irq_, irq, _, routine)	\
	__attribute__((section(".rodata.irqs")))= {IRQ_HANDLER(irq), irq};\
	__asm__ (							\
		".section .text._irq_"#irq"_handler\n"			\
		"_irq_"#irq"_handler:\n"				\
			"pusha\n"					\
			"add  $1, __in_isr\n"				\
			"call "#routine"\n"				\
			"push $0\n"					\
			"push $0\n"					\
			"call switch_handler\n"				\
			"addl $0x08, %esp\n"				\
			"test %eax, %eax\n"				\
			"je 1f\n"					\
			"movl current_task, %eax\n"			\
			"movl %esp, (%eax)\n"				\
			"movl next_task, %eax\n"			\
			"movl %eax, current_task\n"			\
			"movl (%eax), %esp\n"				\
			"1:\n"						\
			"movl $"#vector ", (0xFEC00040)\n"              \
			"sub  $1, __in_isr\n"				\
			"movl $0x00, (0xFEE000B0)\n" 			\
			"popa\n"					\
			"iret\n"					\
		);

#endif  /* __CROS_EC_IRQ_HANDLER_H */
