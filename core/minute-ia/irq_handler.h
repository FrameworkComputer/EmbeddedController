/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Helper to declare IRQ handling routines */

#ifndef __CROS_EC_IRQ_HANDLER_H
#define __CROS_EC_IRQ_HANDLER_H

#include "registers.h"

#ifdef CONFIG_FPU
#define save_fpu_ctx	"fnsave 20(%eax)\n"
#define rstr_fpu_ctx	"frstor 20(%eax)\n"
#else
#define save_fpu_ctx
#define rstr_fpu_ctx
#endif

#ifdef CONFIG_TASK_PROFILING
#define task_start_irq_handler_call "call task_start_irq_handler\n"
#else
#define task_start_irq_handler_call
#endif


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
 * Note: currently we don't allow nested irq handling
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
			ASM_LOCK_PREFIX "addl  $1, __in_isr\n"		\
			"movl %esp, %eax\n"                             \
			"movl $stack_end, %esp\n"                       \
			"push %eax\n"                                   \
			task_start_irq_handler_call			\
			"call "#routine"\n"				\
			"push $0\n"					\
			"push $0\n"					\
			"call switch_handler\n"				\
			"addl $0x08, %esp\n"				\
			"pop %esp\n"                                    \
			"test %eax, %eax\n"				\
			"je 1f\n"					\
			"movl current_task, %eax\n"			\
			save_fpu_ctx 					\
			"movl %esp, (%eax)\n"				\
			"movl next_task, %eax\n"			\
			"movl %eax, current_task\n"			\
			"movl (%eax), %esp\n"				\
			rstr_fpu_ctx					\
			"1:\n"						\
			"movl $"#vector ", (0xFEC00040)\n"              \
			"movl $0x00, (0xFEE000B0)\n" 			\
			ASM_LOCK_PREFIX "subl  $1, __in_isr\n"		\
			"popa\n"					\
			"iret\n"					\
		);
#endif  /* __CROS_EC_IRQ_HANDLER_H */
