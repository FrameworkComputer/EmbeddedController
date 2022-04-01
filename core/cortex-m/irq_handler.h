/* Copyright 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Helper to declare IRQ handling routines */

#ifndef __CROS_EC_IRQ_HANDLER_H
#define __CROS_EC_IRQ_HANDLER_H

#ifdef CONFIG_TASK_PROFILING
#define TASK_START_IRQ_HANDLER(excep_return) \
	task_start_irq_handler(excep_return)
#else
#define TASK_START_IRQ_HANDLER(excep_return)
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
	void IRQ_HANDLER(irq)(void);				\
	typedef struct {					\
		int fake[irq >= CONFIG_IRQ_COUNT ? -1 : 1];	\
	} irq_num_check_##irq;					\
	static void __keep routine(void);			\
	void IRQ_HANDLER(irq)(void)				\
	{							\
		void *ret = __builtin_return_address(0);	\
		TASK_START_IRQ_HANDLER(ret);			\
		routine();					\
		task_resched_if_needed(ret);			\
	}							\
	const struct irq_priority __keep IRQ_PRIORITY(irq)	\
	__attribute__((section(".rodata.irqprio")))		\
			= {irq, priority}
#endif  /* __CROS_EC_IRQ_HANDLER_H */
