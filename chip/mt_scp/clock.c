/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Clocks, PLL and power settings */

#include "clock.h"
#include "common.h"
#include "registers.h"
#include "task.h"
#include "util.h"

void clock_init(void)
{
	/* Set VREQ to HW mode */
	SCP_CPU_VREQ = CPU_VREQ_HW_MODE;
	SCP_SECURE_CTRL &= ~ENABLE_SPM_MASK_VREQ;

	/* Set DDREN auto mode */
	SCP_SYS_CTRL |= AUTO_DDREN;

	/* Initialize 26MHz system clock counter reset value to 1. */
	SCP_CLK_SYS_VAL =
		(SCP_CLK_SYS_VAL & ~CLK_SYS_VAL_MASK) | CLK_SYS_VAL(1);
	/* Initialize high frequency ULPOSC counter reset value to 1. */
	SCP_CLK_HIGH_VAL =
		(SCP_CLK_HIGH_VAL & ~CLK_HIGH_VAL_MASK) | CLK_HIGH_VAL(1);
	/* Initialize sleep mode control VREQ counter. */
	SCP_CLK_SLEEP_CTRL =
		(SCP_CLK_SLEEP_CTRL & ~VREQ_COUNTER_MASK) | VREQ_COUNTER_VAL(1);

	/* Set normal wake clock */
	SCP_WAKE_CKSW &= ~WAKE_CKSW_SEL_NORMAL_MASK;

	/* Enable fast wakeup support */
	SCP_CLK_SLEEP = 0;
	SCP_CLK_ON_CTRL = (SCP_CLK_ON_CTRL & ~HIGH_FINAL_VAL_MASK) |
			  HIGH_FINAL_VAL_DEFAULT;
	SCP_FAST_WAKE_CNT_END =
		(SCP_FAST_WAKE_CNT_END & ~FAST_WAKE_CNT_END_MASK) |
		FAST_WAKE_CNT_END_DEFAULT;

	/* Set slow wake clock */
	SCP_WAKE_CKSW = (SCP_WAKE_CKSW & ~WAKE_CKSW_SEL_SLOW_MASK) |
			WAKE_CKSW_SEL_SLOW_DEFAULT;

	/* Select CLK_HIGH as wakeup clock */
	SCP_CLK_SLOW_SEL = (SCP_CLK_SLOW_SEL &
			    ~(CKSW_SEL_SLOW_MASK | CKSW_SEL_SLOW_DIV_MASK)) |
			   CKSW_SEL_SLOW_ULPOSC2_CLK;

	/*
	 * Set legacy wakeup
	 *   - disable SPM sleep control
	 *   - disable SCP sleep mode
	 */
	SCP_CLK_SLEEP_CTRL &= ~(EN_SLEEP_CTRL | SPM_SLEEP_MODE);

	task_enable_irq(SCP_IRQ_CLOCK);
	task_enable_irq(SCP_IRQ_CLOCK2);
}

void clock_control_irq(void)
{
	/* Read ack CLK_IRQ */
	(SCP_CLK_IRQ_ACK);
	task_clear_pending_irq(SCP_IRQ_CLOCK);
}
DECLARE_IRQ(SCP_IRQ_CLOCK, clock_control_irq, 3);

void clock_fast_wakeup_irq(void)
{
	/* Ack fast wakeup */
	SCP_SLEEP_IRQ2 = 1;
	task_clear_pending_irq(SCP_IRQ_CLOCK2);
}
DECLARE_IRQ(SCP_IRQ_CLOCK2, clock_fast_wakeup_irq, 3);
