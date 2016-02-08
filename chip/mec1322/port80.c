/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Port 80 Timer Interrupt for MEC1322 */

#include "common.h"
#include "console.h"
#include "hooks.h"
#include "lpc.h"
#include "port80.h"
#include "registers.h"
#include "task.h"

void port_80_interrupt(void)
{
	int data;
	MEC1322_TMR16_STS(1) = 1; /* Ack the interrupt */
	if ((1 << 1) & MEC1322_INT_RESULT(23)) {
		data = port_80_read();

		if (data != PORT_80_IGNORE)
			port_80_write(data);
	}
}
DECLARE_IRQ(MEC1322_IRQ_TIMER16_1, port_80_interrupt, 2);

/*
 * The port 80 interrupt will use TIMER16 instance 1 for a 1ms countdown
 * timer.  This timer is on GIRQ23, bit 1.
 */
static void port_80_interrupt_init(void)
{
	uint32_t val = 0;

	/*
	 * The timers are driven by a 48MHz oscillator.  Prescale down to
	 * 1MHz. 48MHz/48 -> 1MHz
	 */
	val = MEC1322_TMR16_CTL(1);
	val = (val & 0xFFFF) | (47 << 16);
	/* Automatically restart the timer. */
	val |= (1 << 3);
	/* The counter should decrement. */
	val &= ~(1 << 2);
	MEC1322_TMR16_CTL(1) = val;

	/* Set the reload value to 1000us. (1ms). */
	MEC1322_TMR16_PRE(1) = 1000;

	/* Clear the status if any. */
	MEC1322_TMR16_STS(1) |= 1;

	/* Clear any pending interrupt. */
	MEC1322_INT_SOURCE(23) = (1 << 1);
	/* Enable IRQ vector 23. */
	MEC1322_INT_BLK_EN |= (1 << 23);
	/* Enable the interrupt. */
	MEC1322_TMR16_IEN(1) |= 1;
	MEC1322_INT_ENABLE(23) = (1 << 1);
	task_enable_irq(MEC1322_IRQ_TIMER16_1);

	/* Enable and start the timer. */
	MEC1322_TMR16_CTL(1) |= 1 | (1 << 5);
}
DECLARE_HOOK(HOOK_INIT, port_80_interrupt_init, HOOK_PRIO_DEFAULT);

static void port_80_interrupt_enable(void)
{
	/* Enable the interrupt. */
	task_enable_irq(MEC1322_IRQ_TIMER16_1);
	/* Enable the timer block. */
	MEC1322_TMR16_CTL(1) |= 1;
}
DECLARE_HOOK(HOOK_CHIPSET_RESUME, port_80_interrupt_enable, HOOK_PRIO_DEFAULT);

static void port_80_interrupt_disable(void)
{
	/* Disable the timer block. */
	MEC1322_TMR16_CTL(1) &= ~1;
	/* Disable the interrupt. */
	task_disable_irq(MEC1322_IRQ_TIMER16_1);
}
DECLARE_HOOK(HOOK_CHIPSET_SUSPEND, port_80_interrupt_disable,
	     HOOK_PRIO_DEFAULT);
