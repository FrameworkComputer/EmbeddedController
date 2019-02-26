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

static void scp_ulposc_config(int osc)
{
	/* TODO(b/120176040): add ULPOSC calibration */
	const struct {
		uint8_t div;
		uint8_t cali;
	} ulposc_config[] = {
		{ .div = 12, .cali = 32},
		{ .div = 16, .cali = 32},
	};
	const int osc_index = osc - 1;
	uint32_t val;

	if (osc != 1 && osc != 2)
		return;

	/* Clear all bits */
	val = 0;
	/* Enable CP */
	val |= OSC_CP_EN;
	/* Set div */
	val |= ulposc_config[osc_index].div << 17;
	/* F-band = 0, I-band = 4 */
	val |= 4 << 6;
	/* Set calibration */
	val |= ulposc_config[osc_index].cali;
	/* Set control register 1 */
	AP_ULPOSC_CON02(osc) = val;
	/* Set control register 2, enable div2 */
	AP_ULPOSC_CON13(osc) |= OSC_DIV2_EN;
}

void scp_set_clock_high(int osc, int on)
{
	if (on) {
		switch (osc) {
		case 1:
			/* Enable ULPOSC */
			SCP_CLK_EN |= EN_CLK_HIGH;
			/* TODO: Turn on clock gate after 25ms */
			SCP_CLK_EN |= CG_CLK_HIGH;
			break;
		case 2:
			/* Enable ULPOSC1 & ULPOSC2 */
			SCP_CLK_EN |= EN_CLK_HIGH;
			SCP_CLK_ON_CTRL &= ~HIGH_CORE_DIS_SUB;
			/* TODO: Turn on clock gate after 25ms */
			SCP_CLK_HIGH_CORE |= 1;
			break;
		default:
			break;
		}
	} else {
		switch (osc) {
		case 1:
			/* Disable clock gate */
			SCP_CLK_EN &= CG_CLK_HIGH;
			/* TODO: Turn off ULPOSC1 after 50us */
			SCP_CLK_EN &= EN_CLK_HIGH;
			break;
		case 2:
			SCP_CLK_HIGH_CORE &= ~1;
			/* TODO: Turn off ULPOSC1 after 50us */
			SCP_CLK_ON_CTRL |= HIGH_CORE_DIS_SUB;
			break;
		default:
			break;
		}
	}
	/* TODO: Wait 25us */
}

void scp_enable_clock(void)
{
	/* VREQ */
	SCP_CPU_VREQ = 0x10001;
	SCP_SECURE_CTRL &= ~ENABLE_SPM_MASK_VREQ;

	/* DDREN auto mode */
	SCP_SYS_CTRL |= AUTO_DDREN;

	/* Set settle time */
	SCP_CLK_SYS_VAL = 1;  /* System clock */
	SCP_CLK_HIGH_VAL = 1; /* ULPOSC */
	SCP_CLK_SLEEP_CTRL = (SCP_CLK_SLEEP_CTRL & ~VREQ_COUNTER_MASK) | 2;

	/* Disable slow wake */
	SCP_CLK_SLEEP = SLOW_WAKE_DISABLE;
	/* Disable SPM sleep control, disable sleep mode */
	SCP_CLK_SLEEP_CTRL &= ~(SPM_SLEEP_MODE | EN_SLEEP_CTRL);

	/* Turn off ULPOSC2 */
	SCP_CLK_ON_CTRL |= HIGH_CORE_DIS_SUB;
	scp_ulposc_config(1);
	scp_set_clock_high(1, 1); /* Turn on ULPOSC1 */
	scp_ulposc_config(2);
	scp_set_clock_high(2, 1); /* Turn on ULPOSC2 */

	/* Enable default clock gate */
	SCP_CLK_GATE |= CG_DMA_CH3 | CG_DMA_CH2 | CG_DMA_CH1 | CG_DMA_CH0 |
			CG_I2C_M | CG_MAD_M;
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
