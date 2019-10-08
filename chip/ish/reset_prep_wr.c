/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Workaround for ISH5.4 reset prep handling before full PM is enabled */
#include "common.h"
#include "hooks.h"
#include "interrupts.h"
#include "registers.h"
#include "system.h"
#include "task.h"

/*
 * IRQ fires when we receive a RESET_PREP message from AP. This happens at S0
 * entry.
 */
static void reset_prep_wr_isr(void)
{
	system_reset(SYSTEM_RESET_HARD);
}
DECLARE_IRQ(ISH_RESET_PREP_IRQ, reset_prep_wr_isr);

void reset_prep_init(void)
{
	/* Clear reset bit */
	ISH_RST_REG = 0;

	/* Clear reset history register from previous boot. */
	CCU_RST_HST = CCU_RST_HST;
	/* Unmask reset prep avail interrupt mask */
	PMU_RST_PREP = 0;
	/* Clear TCG Enable, no trunk level clock gating*/
	CCU_TCG_ENABLE = 0;
	/* Clear BCG Enable, no block level clock gating*/
	CCU_BCG_ENABLE = 0;

	task_enable_irq(ISH_RESET_PREP_IRQ);
}
DECLARE_HOOK(HOOK_INIT, reset_prep_init, HOOK_PRIO_DEFAULT);
