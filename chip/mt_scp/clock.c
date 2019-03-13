/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Clocks, PLL and power settings */

#include "clock.h"
#include "clock_chip.h"
#include "common.h"
#include "console.h"
#include "registers.h"
#include "task.h"
#include "timer.h"
#include "util.h"

#define CPRINTF(format, args...) cprintf(CC_CLOCK, format, ## args)

/* Default ULPOSC clock speed in Hz */
#ifndef ULPOSC1_CLOCK_HZ
#define ULPOSC1_CLOCK_HZ 250000000
#endif
#ifndef ULPOSC2_CLOCK_HZ
#define ULPOSC2_CLOCK_HZ 330000000
#endif

#define ULPOSC_DIV_MAX (1 << OSC_DIV_BITS)
#define ULPOSC_CALI_MAX (1 << OSC_CALI_BITS)

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

static void scp_ulposc_config(int osc, uint32_t osc_div, uint32_t osc_cali)
{
	uint32_t val;

	/* Clear all bits */
	val = 0;
	/* Enable CP */
	val |= OSC_CP_EN;
	/* Set div */
	val |= osc_div << 17;
	/* F-band = 0, I-band = 4 */
	val |= 4 << 6;
	/* Set calibration */
	val |= osc_cali;
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

static unsigned int scp_measure_ulposc_freq(int osc)
{
	unsigned int result = 0;

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

	/*
	 * Frequency meter counts cycles in 1 / (26 * 1000) second period.
	 *   freq_in_hz = freq_counter * 26 * 1000
	 *
	 * The hardware takes 38us to count cycles. Delay 50us then check
	 * METER_RUN flag.
	 */
	udelay(50);
	if (!(AP_SCP_CFG_0 & CFG_FREQ_METER_RUN))
		result = CFG_FREQ_COUNTER(AP_SCP_CFG_1);

	/* Disable freq meter */
	AP_SCP_CFG_0 &= ~CFG_FREQ_METER_ENABLE;
	return result;
}

static inline int signum(int v)
{
	return (v > 0) - (v < 0);
}

static inline int abs(int v)
{
	return (v >= 0) ? v : -v;
}

static int scp_ulposc_config_measure(int osc, int div, int cali)
{
	int freq;

	scp_ulposc_config(osc, div, cali);
	freq = scp_measure_ulposc_freq(osc);
	CPRINTF("ULPOSC%d: %d %d %d (%dMHz)\n",
		osc + 1, div, cali, freq,
		freq * 26 / 1000);

	return freq;
}

/**
 * Calibrate ULPOSC to target frequency.
 *
 * @param osc           0:ULPOSC1, 1:ULPOSC2
 * @param target_hz     Target frequency to set
 * @return              Frequency counter output
 *
 */
static int scp_calibrate_ulposc(int osc, int target_hz)
{
	int target_freq = target_hz / (26 * 1000);
	struct ulposc {
		int div;        /* frequency divisor/multiplier */
		int cali;       /* variable resistor calibrator */
		int freq;       /* frequency counter measure result */
		int inc;        /* next div or cali param diff */
	} curr, prev = {0};

	curr.div = ULPOSC_DIV_MAX / 2;
	curr.cali = ULPOSC_CALI_MAX / 2;

	/*
	 * In the loop below, linear search closest div value to get desired
	 * frequency counter value. Then adjust cali to get a better result.
	 * Note that this doesn't give optimal output frequency. The search
	 * starts on cali==CALI_MAX/2 line to find best div value, then linear
	 * search cali value with a fixed div. The output result is usually
	 * good enough for core clock.
	 */
	while (1) {
		curr.freq = scp_ulposc_config_measure(osc, curr.div, curr.cali);

		if (!curr.freq)
			return 0;
		if (curr.freq == target_freq)
			return curr.freq;

		/* Linear search is enough for both div and cali params */
		curr.inc = signum(target_freq - curr.freq);

		/* Search div value */
		if (prev.div != curr.div) {
			if (!prev.freq) {
				prev = curr;
				if (target_freq > curr.freq) {
					curr.div++;
					curr.inc = prev.inc = 1;
				} else {
					curr.div--;
					curr.inc = prev.inc = -1;
				}
				continue;
			}
			if (curr.inc == prev.inc) {
				prev = curr;
				curr.div += curr.inc;
				if (curr.div < 0 || curr.div >= ULPOSC_DIV_MAX)
					return 0;
			} else {
				prev = curr;
				curr.cali += curr.inc;
			}
			continue;
		}

		/* Search cali value */
		if (curr.inc == prev.inc) {
			prev = curr;
			curr.cali += curr.inc;
			if (curr.cali < 0 || curr.cali >= ULPOSC_CALI_MAX)
				return 0;
			continue;
		}

		if (abs(target_freq - curr.freq) >
		    abs(target_freq - prev.freq)) {
			scp_ulposc_config_measure(osc, prev.div, prev.cali);
			return prev.freq;
		}
	}

	return 0;
}

static void scp_clock_high_enable(int osc)
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
	/* Select default CPU clock */
	SCP_CLK_SEL = CLK_SEL_SYS_26M;

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
	scp_ulposc_config(0, ULPOSC_DIV_MAX / 2, ULPOSC_CALI_MAX / 2);
	scp_clock_high_enable(0); /* Turn on ULPOSC1 */
	scp_ulposc_config(1, ULPOSC_DIV_MAX / 2, ULPOSC_CALI_MAX / 2);
	scp_clock_high_enable(1); /* Turn on ULPOSC2 */

	/* Calibrate ULPOSC */
	scp_calibrate_ulposc(0, ULPOSC1_CLOCK_HZ);
	scp_calibrate_ulposc(1, ULPOSC2_CLOCK_HZ);

	/* Select ULPOSC2 high speed CPU clock */
	SCP_CLK_SEL = CLK_SEL_ULPOSC_2;

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

/* Console command */
int command_ulposc(int argc, char *argv[])
{
	if (argc > 1 && !strncmp(argv[1], "cal", 3)) {
		scp_calibrate_ulposc(0, ULPOSC1_CLOCK_HZ);
		scp_calibrate_ulposc(1, ULPOSC2_CLOCK_HZ);
	}

	/* SCP clock meter counts every (26MHz / 1000) tick */
	ccprintf("ULPOSC1 frequency: %u MHz\n",
		 scp_measure_ulposc_freq(0) * 26 / 1000);
	ccprintf("ULPOSC2 frequency: %u MHz\n",
		 scp_measure_ulposc_freq(1) * 26 / 1000);

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(ulposc, command_ulposc, "[calibrate]",
			"Calibrate ULPOSC frequency");

