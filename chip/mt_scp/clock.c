/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Clocks, PLL and power settings */

#include "clock.h"
#include "common.h"
#include "console.h"
#include "registers.h"
#include "task.h"
#include "timer.h"
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

/* TODO(b/120176040): add ULPOSC calibration */
static const struct {
	uint8_t div;
	uint8_t cali;
} ulposc_config[] = {
	/* Default config */
	{ .div = 12, .cali = 32},
	{ .div = 16, .cali = 32},
};

static void scp_ulposc_config(int osc)
{
	uint32_t val;

	if (osc != 0 && osc != 1)
		return;

	/* Clear all bits */
	val = 0;
	/* Enable CP */
	val |= OSC_CP_EN;
	/* Set div */
	val |= ulposc_config[osc].div << 17;
	/* F-band = 0, I-band = 4 */
	val |= 4 << 6;
	/* Set calibration */
	val |= ulposc_config[osc].cali;
	/* Set control register 1 */
	AP_ULPOSC_CON02(osc) = val;
	/* Set control register 2, enable div2 */
	AP_ULPOSC_CON13(osc) |= OSC_DIV2_EN;
}

static inline void busy_udelay(int usec)
{
	/*
	 * Delaying by busy-looping, for place that can't use udelay because of
	 * the clock not configured yet. The value 28 is chosen approximately
	 * from experiment.
	 */
	volatile int i = usec * 28;

	while (i--)
		;
}

void scp_clock_high_enable(int osc)
{
	/* Enable high speed clock */
	SCP_CLK_EN |= EN_CLK_HIGH;

	switch (osc) {
	case 0:
		/* After 25ms, enable ULPOSC */
		busy_udelay(25 * MSEC);
		SCP_CLK_EN |= CG_CLK_HIGH;
		break;
	case 1:
		/* Turn off ULPOSC2 high-core-disable switch */
		SCP_CLK_ON_CTRL &= ~HIGH_CORE_DIS_SUB;
		/* After 25ms, turn on ULPOSC2 high core clock gate */
		busy_udelay(25 * MSEC);
		SCP_CLK_HIGH_CORE |= CLK_HIGH_CORE_CG;
		break;
	default:
		break;
	}
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
	scp_ulposc_config(0);
	scp_clock_high_enable(0); /* Turn on ULPOSC1 */
	scp_ulposc_config(1);
	scp_clock_high_enable(1); /* Turn on ULPOSC2 */

	/* Enable default clock gate */
	SCP_CLK_GATE |= CG_DMA_CH3 | CG_DMA_CH2 | CG_DMA_CH1 | CG_DMA_CH0 |
			CG_I2C_M | CG_MAD_M;
}

unsigned int clock_measure_ulposc_freq(int osc)
{
	timestamp_t deadline;
	unsigned int result = 0;

	if (osc != 0 && osc != 1)
		return result;

	/* Before select meter clock input, bit[1:0] = b00 */
	AP_CLK_DBG_CFG = (AP_CLK_DBG_CFG & ~DBG_MODE_MASK) |
			 DBG_MODE_SET_CLOCK;

	/* Select source, bit[21:16] = clk_src */
	AP_CLK_DBG_CFG = (AP_CLK_DBG_CFG & ~DBG_BIST_SOURCE_MASK) |
			 (osc == 0 ? DBG_BIST_SOURCE_ULPOSC1 :
				     DBG_BIST_SOURCE_ULPOSC2);

	/* Set meter divisor to 1, bit[31:24] = b00000000 */
	AP_CLK_MISC_CFG_0 = (AP_CLK_MISC_CFG_0 & ~MISC_METER_DIVISOR_MASK) |
			    MISC_METER_DIV_1;

	/* Enable frequency meter, without start */
	AP_SCP_CFG_0 |= CFG_FREQ_METER_ENABLE;

	/* Trigger frequency meter start */
	AP_SCP_CFG_0 |= CFG_FREQ_METER_RUN;

	deadline.val = get_time().val + 400 * MSEC;
	while (AP_SCP_CFG_0 & CFG_FREQ_METER_RUN) {
		if (timestamp_expired(deadline, NULL))
			goto exit;
		msleep(1);
	}

	/* Get result */
	result = CFG_FREQ_COUNTER(AP_SCP_CFG_1);

exit:
	/* Disable freq meter */
	AP_SCP_CFG_0 &= ~CFG_FREQ_METER_ENABLE;
	return result;
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

/* Console command */
int command_ulposc(int argc, char *argv[])
{
	/* SCP clock meter counts every (26MHz / 1024) tick */
	ccprintf("ULPOSC1 frequency: %u MHz\n",
		 clock_measure_ulposc_freq(0) * 26 / 1024);
	ccprintf("ULPOSC2 frequency: %u MHz\n",
		 clock_measure_ulposc_freq(1) * 26 / 1024);

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(ulposc, command_ulposc, NULL, NULL);
