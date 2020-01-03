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
	int cnt;

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
	 * Frequency meter counts cycles in 1 / (26 * 1024) second period.
	 *   freq_in_hz = freq_counter * 26 * 1024
	 *
	 * The hardware takes 38us to count cycles. Delay up to 100us,
	 * as busy_udelay may not be accurate when sysclk is not 26Mhz
	 * (e.g. when recalibrating/measuring after boot).
	 */
	for (cnt = 100; cnt; cnt--) {
		busy_udelay(1);
		if (!(AP_SCP_CFG_0 & CFG_FREQ_METER_RUN)) {
			result = CFG_FREQ_COUNTER(AP_SCP_CFG_1);
			break;
		}
	}

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
	CPRINTF("ULPOSC%d: %d %d %d (%dkHz)\n",
		osc + 1, div, cali, freq,
		freq * 26 * 1000 / 1024);

	return freq;
}

/**
 * Calibrate ULPOSC to target frequency.
 *
 * @param osc           0:ULPOSC1, 1:ULPOSC2
 * @param target_mhz    Target frequency to set
 * @return              Frequency counter output
 *
 */
static int scp_calibrate_ulposc(int osc, int target_mhz)
{
	int target_freq = DIV_ROUND_NEAREST(target_mhz * 1024, 26);
	struct ulposc {
		int div;        /* frequency divisor/multiplier */
		int cali;       /* variable resistor calibrator */
		int freq;       /* frequency counter measure result */
	} curr, prev = {0};
	enum { STAGE_DIV, STAGE_CALI } stage = STAGE_DIV;
	int param, param_max;

	curr.div = ULPOSC_DIV_MAX / 2;
	curr.cali = ULPOSC_CALI_MAX / 2;

	param = curr.div;
	param_max = ULPOSC_DIV_MAX;

	/*
	 * In the loop below, linear search closest div value to get desired
	 * frequency counter value. Then adjust cali to get a better result.
	 * Note that this doesn't give optimal output frequency, but it's
	 * usually close enough.
	 * TODO(b:120176040): See if we can efficiently calibrate the clock with
	 * more precision by exploring more of the cali/div space.
	 *
	 * The frequency function follows. Note that f is positively correlated
	 * with both div and cali:
	 * f(div, cali) = k1 * (div + k2) / R(cali) * C
	 * Where:
	 *   R(cali) = k3 / (1 + k4 * (cali - k4))
	 */
	while (1) {
		curr.freq = scp_ulposc_config_measure(osc, curr.div, curr.cali);

		if (!curr.freq)
			return 0;

		/*
		 * If previous and current are on either side of the desired
		 * frequency, pick the closest one.
		 */
		if (prev.freq && signum(target_freq - curr.freq) !=
				 signum(target_freq - prev.freq)) {
			if (abs(target_freq - prev.freq) <
					abs(target_freq - curr.freq))
				curr = prev;

			if (stage == STAGE_CALI)
				break;

			/* Switch to optimizing cali */
			stage = STAGE_CALI;
			param = curr.cali;
			param_max = ULPOSC_CALI_MAX;
		}

		prev = curr;
		param += signum(target_freq - curr.freq);

		if (param < 0 || param >= param_max)
			return 0;

		if (stage == STAGE_DIV)
			curr.div = param;
		else
			curr.cali = param;
	}

	/*
	 * It's possible we end up using prev, so reset the configuration and
	 * measure again.
	 */
	return scp_ulposc_config_measure(osc, curr.div, curr.cali);
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

void scp_use_clock(enum scp_clock_source src)
{
	/*
	 * DIV2 divider takes precedence over clock selection to prevent
	 * over-clocking.
	 */
	if (src == SCP_CLK_ULPOSC1)
		SCP_CLK_DIV_SEL = CLK_DIV2;

	SCP_CLK_SEL = src;

	if (src != SCP_CLK_ULPOSC1)
		SCP_CLK_DIV_SEL = CLK_DIV1;
}

void scp_enable_clock(void)
{
	/* Select default CPU clock */
	scp_use_clock(SCP_CLK_26M);

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
	scp_ulposc_config(0, 12, 32);
	scp_clock_high_enable(0); /* Turn on ULPOSC1 */
	scp_ulposc_config(1, 16, 32);
	scp_clock_high_enable(1); /* Turn on ULPOSC2 */

	/* Calibrate ULPOSC */
	scp_calibrate_ulposc(0, ULPOSC1_CLOCK_MHZ);
	scp_calibrate_ulposc(1, ULPOSC2_CLOCK_MHZ);

	/* Select ULPOSC2 high speed CPU clock */
	scp_use_clock(SCP_CLK_ULPOSC2);

	/* Enable default clock gate */
	SCP_CLK_GATE |= CG_DMA_CH3 | CG_DMA_CH2 | CG_DMA_CH1 | CG_DMA_CH0 |
			CG_I2C_M | CG_MAD_M | CG_AP2P_M;

	/* Select pwrap_ulposc */
	AP_CLK_CFG_5 = (AP_CLK_CFG_5 & ~PWRAP_ULPOSC_MASK) | OSC_D16;

	/* Enable pwrap_ulposc clock gate */
	AP_CLK_CFG_5_CLR = PWRAP_ULPOSC_CG;
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
		scp_calibrate_ulposc(0, ULPOSC1_CLOCK_MHZ);
		scp_calibrate_ulposc(1, ULPOSC2_CLOCK_MHZ);
	}

	/* SCP clock meter counts every (26MHz / 1024) tick */
	ccprintf("ULPOSC1 frequency: %u kHz\n",
		 scp_measure_ulposc_freq(0) * 26 * 1000 / 1024);
	ccprintf("ULPOSC2 frequency: %u kHz\n",
		 scp_measure_ulposc_freq(1) * 26 * 1000 / 1024);

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(ulposc, command_ulposc, "[calibrate]",
			"Calibrate ULPOSC frequency");
