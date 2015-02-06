/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Helper to declare IRQ handling routines */

#ifndef __IRQ_HANDLER_H
#define __IRQ_HANDLER_H

#include "cpu.h"

/* Helper macros to build the IRQ handler and priority struct names */
#define IRQ_HANDLER(irqname) CONCAT3(irq_, irqname, _handler)
#define IRQ_PRIORITY(irqname) CONCAT2(prio_, irqname)

/*
 * Macro to connect the interrupt handler "routine" to the irq number "irq" and
 * ensure it is enabled in the interrupt controller with the right priority.
 */
#define DECLARE_IRQ(irq, routine, priority) DECLARE_IRQ_(irq, routine, priority)
#ifdef CONFIG_TASK_PROFILING
#define DECLARE_IRQ_(irq, routine, priority)                    \
	void IRQ_HANDLER(irq)(void)				\
	{							\
		void *ret = __builtin_return_address(0);	\
		task_start_irq_handler(ret);			\
		routine();					\
		task_end_irq_handler(ret);			\
	}							\
	const struct irq_priority IRQ_PRIORITY(irq)		\
	__attribute__((section(".rodata.irqprio")))		\
			= {irq, priority}
#else /* CONFIG_TASK_PROFILING */
/* No Profiling : connect directly the IRQ vector */
#define DECLARE_IRQ_(irq, routine, priority)                    \
	void IRQ_HANDLER(irq)(void) __attribute__((alias(STRINGIFY(routine))));\
	const struct irq_priority IRQ_PRIORITY(irq)		\
	__attribute__((section(".rodata.irqprio")))		\
			= {irq, priority}
#endif /* CONFIG_TASK_PROFILING */
#endif  /* __IRQ_HANDLER_H */
