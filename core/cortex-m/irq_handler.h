/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Helper to declare IRQ handling routines */

#ifndef __IRQ_HANDLER_H
#define __IRQ_HANDLER_H

#ifdef CONFIG_TASK_PROFILING
#define bl_task_start_irq_handler "bl task_start_irq_handler\n"
#else
#define bl_task_start_irq_handler ""
#endif

/* Helper macros to build the IRQ handler and priority struct names */
#define IRQ_HANDLER(irqname) CONCAT3(irq_, irqname, _handler)
#define IRQ_PRIORITY(irqname) CONCAT2(prio_, irqname)
/*
 * Macro to connect the interrupt handler "routine" to the irq number "irq" and
 * ensure it is enabled in the interrupt controller with the right priority.
 */
#define DECLARE_IRQ(irq, routine, priority) DECLARE_IRQ_(irq, routine, priority)
#define DECLARE_IRQ_(irq, routine, priority)                    \
	void IRQ_HANDLER(irq)(void) __attribute__((naked));	\
	void IRQ_HANDLER(irq)(void)				\
	{							\
		asm volatile("mov r0, lr\n"			\
			     "push {r0, lr}\n"			\
			     bl_task_start_irq_handler		\
			     "bl "#routine"\n"			\
			     "pop {r0, lr}\n"			\
			     "b task_resched_if_needed\n"	\
			    );					\
	}							\
	const struct irq_priority IRQ_PRIORITY(irq)		\
	__attribute__((section(".rodata.irqprio")))		\
			= {irq, priority}
#endif  /* __IRQ_HANDLER_H */
