/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <arch/arm/aarch32/cortex_m/cmsis.h>
#include "console.h"

/* IRQ counters */
int irq_count[CONFIG_NUM_IRQS];

void sys_trace_isr_enter_user(int nested_interrupts)
{
	ARG_UNUSED(nested_interrupts);

	/* read the exception number */
	uint32_t irq = __get_IPSR() - 16;

	__ASSERT(irq < CONFIG_NUM_IRQS, "Invalid IRQ number");

	irq_count[irq]++;
}

static int command_irq(int argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	for (int i = 0; i < CONFIG_NUM_IRQS; i++) {
		if (irq_count[i])
			ccprintf("  IRQ %d: %d\n", i, irq_count[i]);
	}

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(irq, command_irq, "", "List irq counters");
