/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Clocks, PLL and power settings */

#include <assert.h>
#include <string.h>

#include "clock_chip.h"
#include "clock.h"
#include "console.h"
#include "csr.h"
#include "registers.h"
#include "timer.h"

#define CPRINTF(format, args...) cprintf(CC_CLOCK, format, ##args)

static struct opp_ulposc_cfg {
	uint32_t osc;
	uint32_t div;
	uint32_t fband;
	uint32_t mod;
	uint32_t cali;
	uint32_t target_mhz;
} opp[] = {
	{
		.osc = 1, .target_mhz = 196, .div = 20, .fband = 10, .mod = 3,
		.cali = 64,
	},
	{
		.osc = 0, .target_mhz = 260, .div = 14, .fband = 2, .mod = 0,
		.cali = 64,
	},
	{
		.osc = 1, .target_mhz = 280, .div = 20, .fband = 2, .mod = 0,
		.cali = 64,
	},
	{
		.osc = 1, .target_mhz = 360, .div = 20, .fband = 10, .mod = 0,
		.cali = 64,
	},
};

static inline void clock_busy_udelay(int usec)
{
	/*
	 * Delaying by busy-looping, for place that can't use udelay because of
	 * the clock not configured yet. The value 28 is chosen approximately
	 * from experiment.
	 *
	 * `volatile' in order to avoid compiler to optimize the function out
	 * (otherwise, the function will be eliminated).
	 */
	volatile int i = usec * 28;

	while (--i)
		;
}

static void clock_ulposc_config_default(struct opp_ulposc_cfg *opp)
{
	unsigned int val = 0;

	/* set div */
	val |= opp->div << OSC_DIV_SHIFT;
	/* set F-band; I-band = 82 */
	val |= (opp->fband << OSC_FBAND_SHIFT) | (82 << OSC_IBAND_SHIFT);
	/* set calibration */
	val |= opp->cali;
	/* set control register 0 */
	AP_ULPOSC_CON0(opp->osc) = val;

	/* set mod */
	val = opp->mod << OSC_MOD_SHIFT;
	/* rsv2 = 0, rsv1 = 41, cali_32k = 0 */
	val |= 41 << OSC_RSV1_SHIFT;
	/* set control register 1 */
	AP_ULPOSC_CON1(opp->osc) = val;

	/* bias = 64 */
	AP_ULPOSC_CON2(opp->osc) = 64;
}

static void clock_ulposc_config_cali(struct opp_ulposc_cfg *opp,
				     uint32_t cali_val)
{
	uint32_t val;

	val = AP_ULPOSC_CON0(opp->osc);
	val &= ~OSC_CALI_MASK;
	val |= cali_val;
	AP_ULPOSC_CON0(opp->osc) = val;

	clock_busy_udelay(50);
}

static uint32_t clock_ulposc_measure_freq(uint32_t osc)
{
	uint32_t result = 0;
	int cnt;

	/* before select meter clock input, bit[1:0] = b00 */
	AP_CLK_DBG_CFG = (AP_CLK_DBG_CFG & ~DBG_MODE_MASK) |
			 DBG_MODE_SET_CLOCK;

	/* select source, bit[21:16] = clk_src */
	AP_CLK_DBG_CFG = (AP_CLK_DBG_CFG & ~DBG_BIST_SOURCE_MASK) |
			 (osc == 0 ? DBG_BIST_SOURCE_ULPOSC1 :
				     DBG_BIST_SOURCE_ULPOSC2);

	/* set meter divisor to 1, bit[31:24] = b00000000 */
	AP_CLK_MISC_CFG_0 = (AP_CLK_MISC_CFG_0 & ~MISC_METER_DIVISOR_MASK) |
			    MISC_METER_DIV_1;

	/* enable frequency meter, without start */
	AP_SCP_CFG_0 |= CFG_FREQ_METER_ENABLE;

	/* trigger frequency meter start */
	AP_SCP_CFG_0 |= CFG_FREQ_METER_RUN;

	/*
	 * Frequency meter counts cycles in 1 / (26 * 1024) second period.
	 *   freq_in_hz = freq_counter * 26 * 1024
	 *
	 * The hardware takes 38us to count cycles. Delay up to 100us,
	 * as clock_busy_udelay may not be accurate when sysclk is not 26Mhz
	 * (e.g. when recalibrating/measuring after boot).
	 */
	for (cnt = 100; cnt > 0; --cnt) {
		clock_busy_udelay(1);
		if (!(AP_SCP_CFG_0 & CFG_FREQ_METER_RUN)) {
			result = CFG_FREQ_COUNTER(AP_SCP_CFG_1);
			break;
		}
	}

	/* disable freq meter */
	AP_SCP_CFG_0 &= ~CFG_FREQ_METER_ENABLE;

	return result;
}

#define CAL_MIS_RATE	40
static int clock_ulposc_is_calibrated(struct opp_ulposc_cfg *opp)
{
	uint32_t curr, target;

	curr = clock_ulposc_measure_freq(opp->osc);
	target = opp->target_mhz * 1024 / 26;

	/* check if calibrated value is in the range of target value +- 4% */
	if (curr > (target * (1000 - CAL_MIS_RATE) / 1000) &&
	    curr < (target * (1000 + CAL_MIS_RATE) / 1000))
		return 1;
	else
		return 0;
}

static uint32_t clock_ulposc_process_cali(struct opp_ulposc_cfg *opp)
{
	uint32_t current_val = 0;
	uint32_t target_val = opp->target_mhz * 1024 / 26;
	uint32_t middle, min = 0, max = OSC_CALI_MASK;
	uint32_t diff_by_min, diff_by_max, cal_result;

	do {
		middle = (min + max) / 2;
		if (middle == min)
			break;

		clock_ulposc_config_cali(opp, middle);
		current_val = clock_ulposc_measure_freq(opp->osc);

		if (current_val > target_val)
			max = middle;
		else
			min = middle;
	} while (min <= max);

	clock_ulposc_config_cali(opp, min);
	current_val = clock_ulposc_measure_freq(opp->osc);
	if (current_val > target_val)
		diff_by_min = current_val - target_val;
	else
		diff_by_min = target_val - current_val;

	clock_ulposc_config_cali(opp, max);
	current_val = clock_ulposc_measure_freq(opp->osc);
	if (current_val > target_val)
		diff_by_max = current_val - target_val;
	else
		diff_by_max = target_val - current_val;

	if (diff_by_min < diff_by_max)
		cal_result = min;
	else
		cal_result = max;

	clock_ulposc_config_cali(opp, cal_result);
	if (!clock_ulposc_is_calibrated(opp))
		assert(0);

	return cal_result;
}

static void clock_high_enable(int osc)
{
	/* enable high speed clock */
	SCP_CLK_ENABLE |= CLK_HIGH_EN;

	switch (osc) {
	case 0:
		/* after 150us, enable ULPOSC */
		clock_busy_udelay(150);
		SCP_CLK_ENABLE |= CLK_HIGH_CG;
		break;
	case 1:
		/* turn off ULPOSC2 high-core-disable switch */
		SCP_CLK_ON_CTRL &= ~HIGH_CORE_DIS_SUB;
		/* after 150us, turn on ULPOSC2 high core clock gate */
		clock_busy_udelay(150);
		SCP_CLK_HIGH_CORE_CG |= HIGH_CORE_CG;
		clock_busy_udelay(50);
		break;
	default:
		break;
	}
}

static void clock_high_disable(int osc)
{
	switch (osc) {
	case 0:
		SCP_CLK_ENABLE &= ~CLK_HIGH_CG;
		clock_busy_udelay(50);
		SCP_CLK_ENABLE &= ~CLK_HIGH_EN;
		clock_busy_udelay(50);
		break;
	case 1:
		SCP_CLK_HIGH_CORE_CG &= ~HIGH_CORE_CG;
		clock_busy_udelay(50);
		SCP_CLK_ON_CTRL |= HIGH_CORE_DIS_SUB;
		clock_busy_udelay(50);
		break;
	default:
		break;
	}
}

static void clock_calibrate_ulposc(struct opp_ulposc_cfg *opp)
{
	/*
	 * ULPOSC1(osc=0) is already
	 * - calibrated
	 * - enabled in coreboot
	 * - used by pmic wrapper
	 */
	if (opp->osc != 0) {
		clock_high_disable(opp->osc);
		clock_ulposc_config_default(opp);
		clock_high_enable(opp->osc);
	}

	/* Calibrate only if it is not accurate enough. */
	if (!clock_ulposc_is_calibrated(opp))
		opp->cali = clock_ulposc_process_cali(opp);

#ifdef DEBUG
	CPRINTF("osc:%u, target=%uMHz, cal:%u\n",
		opp->osc, opp->target_mhz, opp->cali);
#endif
}

void clock_select_clock(enum scp_clock_source src)
{
	/*
	 * DIV2 divider takes precedence over clock selection to prevent
	 * over-clocking.
	 */
	if (src == SCP_CLK_ULPOSC1)
		SCP_CLK_DIV_SEL = CLK_DIV_SEL2;

	SCP_CLK_SW_SEL = src;

	if (src != SCP_CLK_ULPOSC1)
		SCP_CLK_DIV_SEL = CLK_DIV_SEL1;
}

void clock_init(void)
{
	int i;

	/* select default 26M system clock */
	clock_select_clock(SCP_CLK_26M);

	/* set VREQ to HW mode */
	SCP_CPU_VREQ_CTRL = VREQ_SEL | VREQ_DVFS_SEL;
	SCP_CLK_CTRL_GENERAL_CTRL &= ~VREQ_PMIC_WRAP_SEL;
	SCP_SEC_CTRL &= ~VREQ_SECURE_DIS;

	/* set DDREN to auto mode */
	SCP_SYS_CTRL |= AUTO_DDREN;

	/* set settle time */
	SCP_CLK_SYS_VAL =
		(SCP_CLK_SYS_VAL & ~CLK_SYS_VAL_MASK) | CLK_SYS_VAL_VAL(1);
	SCP_CLK_HIGH_VAL =
		(SCP_CLK_HIGH_VAL & ~CLK_HIGH_VAL_MASK) | CLK_HIGH_VAL_VAL(1);
	SCP_SLEEP_CTRL =
		(SCP_SLEEP_CTRL & ~VREQ_COUNT_MASK) | VREQ_COUNT_VAL(1);

	/* turn off ULPOSC2 */
	SCP_CLK_ON_CTRL |= HIGH_CORE_DIS_SUB;

	/* calibrate ULPOSC */
	for (i = 0; i < ARRAY_SIZE(opp); ++i)
		clock_calibrate_ulposc(&opp[i]);

	/* select ULPOSC2 high speed CPU clock */
	clock_select_clock(SCP_CLK_ULPOSC2);

	/* enable default clock gate */
	SCP_SET_CLK_CG |= CG_DMA_CH3 | CG_DMA_CH2 | CG_DMA_CH1 | CG_DMA_CH0 |
		CG_I2C_MCLK | CG_MAD_MCLK | CG_AP2P_MCLK;
}

#ifdef DEBUG
int command_ulposc(int argc, char *argv[])
{
	int i;

	for (i = 0; i <= 1; ++i)
		ccprintf("ULPOSC%u frequency: %u kHz\n",
			 i + 1,
			 clock_ulposc_measure_freq(i) * 26 * 1000 / 1024);

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(ulposc, command_ulposc, "[ulposc]",
			"Measure ULPOSC frequency");
#endif
