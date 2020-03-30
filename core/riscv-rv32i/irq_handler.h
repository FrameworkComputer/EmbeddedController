/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Helper to declare IRQ handling routines */

#ifndef __CROS_EC_IRQ_HANDLER_H
#define __CROS_EC_IRQ_HANDLER_H

/* Helper macros to build the IRQ handler and priority struct names */
#define IRQ_HANDLER(irqname) CONCAT3(irq_, irqname, _handler)
#define IRQ_PRIORITY(irqname) CONCAT2(prio_, irqname)

#ifndef CPU_INT
#define CPU_INT(irq) irq
#endif

/*
 * Macro to connect the interrupt handler "routine" to the irq number "irq" and
 * ensure it is enabled in the interrupt controller with the right priority.
 */
#define DECLARE_IRQ(irq, routine, priority)				\
	void IRQ_HANDLER(CPU_INT(irq))(void)				\
		__attribute__ ((alias(STRINGIFY(routine))));		\
	const struct irq_priority __keep IRQ_PRIORITY(CPU_INT(irq))	\
	__attribute__((section(".rodata.irqprio")))			\
			= {CPU_INT(irq), priority}

#endif  /* __CROS_EC_IRQ_HANDLER_H */
