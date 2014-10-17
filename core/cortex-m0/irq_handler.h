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

/* re-scheduling flag */
extern int need_resched_or_profiling;

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
	/* Must push registers in pairs to keep 64-bit aligned*/\
	/* stack for ARM EABI. */				\
			     "push {r0, %0}\n"			\
			     bl_task_start_irq_handler		\
			     "bl "#routine"\n"			\
			     "pop {r2, r3}\n"			\
	/* read need_resched_or_profiling result after IRQ */   \
			     "ldr r0, [r3]\n"			\
			     "mov r1, #8\n"			\
			     "cmp r0, #0\n"			\
	/* if we need to go through the re-scheduling, go on */ \
			     "bne 2f\n"				\
	/* else return from exception */			\
			  "1: bx r2\n"				\
	/* check if that's a nested exception */		\
			  "2: tst r1, r2\n"			\
	/* if yes return immediatly */				\
			     "beq 1b\n"				\
			     "push {r0, r2}\n"			\
			     "mov r0, #0\n"			\
			     "mov r1, #0\n"			\
	/* ensure we have priority 0 during re-scheduling */	\
			     "cpsid i\n isb\n"			\
	/* re-schedule the highest priority task */		\
			     "bl svc_handler\n"			\
	/* enable interrupts and return from exception */	\
			     "cpsie i\n"			\
			     "pop {r0,pc}\n"			\
			: : "r"(&need_resched_or_profiling));	\
	}							\
	const struct irq_priority IRQ_PRIORITY(irq)		\
	__attribute__((section(".rodata.irqprio")))		\
			= {irq, priority}
#endif  /* __IRQ_HANDLER_H */
