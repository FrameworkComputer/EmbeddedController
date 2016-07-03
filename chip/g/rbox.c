/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "clock.h"
#include "gpio.h"
#include "hooks.h"
#include "rbox.h"
#include "task.h"

#ifdef CONFIG_RBOX_DEBUG
RBOX_INT(KEY0_IN_FED, "KEY0 pressed");
RBOX_INT(KEY0_IN_RED, "KEY0 released");
RBOX_INT(KEY1_IN_FED, "KEY1 pressed");
RBOX_INT(KEY1_IN_RED, "KEY1 released");
RBOX_INT(PWRB_IN_FED, "PWRB pressed");
RBOX_INT(PWRB_IN_RED, "PWRB released");
RBOX_INT(EC_RST_RED, "EC RST rising");
RBOX_INT(EC_RST_FED, "EC RST falling");
RBOX_INT(AC_PRESENT_RED, "AC attached");
RBOX_INT(AC_PRESENT_FED, "AC detached");

RBOX_INT(BUTTON_COMBO0_RDY, "COMBO0");
RBOX_INT(BUTTON_COMBO1_RDY, "COMBO1");
RBOX_INT(BUTTON_COMBO2_RDY, "COMBO2");

static void enable_interrupts(void)
{
	ENABLE_INT_RF(ENTERING_RW);
	ENABLE_INT_RF(AC_PRESENT);
	ENABLE_INT_RF(PWRB_IN);
	ENABLE_INT_RF(KEY1_IN);
	ENABLE_INT_RF(KEY0_IN);
	ENABLE_INT_RF(EC_RST);
	ENABLE_INT(BUTTON_COMBO0_RDY);
	ENABLE_INT(BUTTON_COMBO1_RDY);
	ENABLE_INT(BUTTON_COMBO2_RDY);

	task_enable_irq(GC_IRQNUM_RBOX0_INTR_AC_PRESENT_FED_INT);
	task_enable_irq(GC_IRQNUM_RBOX0_INTR_AC_PRESENT_RED_INT);

	task_enable_irq(GC_IRQNUM_RBOX0_INTR_PWRB_IN_FED_INT);
	task_enable_irq(GC_IRQNUM_RBOX0_INTR_PWRB_IN_RED_INT);

	task_enable_irq(GC_IRQNUM_RBOX0_INTR_KEY0_IN_RED_INT);
	task_enable_irq(GC_IRQNUM_RBOX0_INTR_KEY0_IN_FED_INT);

	task_enable_irq(GC_IRQNUM_RBOX0_INTR_KEY1_IN_RED_INT);
	task_enable_irq(GC_IRQNUM_RBOX0_INTR_KEY1_IN_FED_INT);

	task_enable_irq(GC_IRQNUM_RBOX0_INTR_EC_RST_RED_INT);
	task_enable_irq(GC_IRQNUM_RBOX0_INTR_EC_RST_FED_INT);

	task_enable_irq(GC_IRQNUM_RBOX0_INTR_BUTTON_COMBO0_RDY_INT);
	task_enable_irq(GC_IRQNUM_RBOX0_INTR_BUTTON_COMBO1_RDY_INT);
	task_enable_irq(GC_IRQNUM_RBOX0_INTR_BUTTON_COMBO2_RDY_INT);
}
#endif

void rbox_init(void)
{
	/* Enable RBOX */
	clock_enable_module(MODULE_RBOX, 1);

	/* Clear existing interrupts */
	GWRITE(RBOX, WAKEUP_CLEAR, 1);
	GWRITE(RBOX, WAKEUP_CLEAR, 0);
	GWRITE(RBOX, INT_STATE, 1);

#ifdef CONFIG_RBOX_DEBUG
	enable_interrupts();
#endif
}
DECLARE_HOOK(HOOK_INIT, rbox_init, HOOK_PRIO_DEFAULT - 1);
