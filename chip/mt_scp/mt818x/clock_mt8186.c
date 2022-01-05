/* Copyright 2022 The Chromium OS Authors. All rights reserved.
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

#define ULPOSC_CAL_MIN_VALUE   3
#define ULPOSC_CAL_MAX_VALUE   60
#define ULPOSC_CAL_START_VALUE ((ULPOSC_CAL_MIN_VALUE + ULPOSC_CAL_MAX_VALUE)/2)

static struct opp_ulposc_cfg {
	uint32_t osc;
	uint32_t div;
	uint32_t iband;
	uint32_t mod;
	uint32_t cali;
	uint32_t target_mhz;
} opp[] = {
	{
		.osc = 1, .target_mhz = ULPOSC2_CLOCK_MHZ, .div = 16, .iband = 4, .mod = 1,
		.cali = ULPOSC_CAL_START_VALUE,
	},
	{
		.osc = 0, .target_mhz = ULPOSC1_CLOCK_MHZ, .div = 12, .iband = 4, .mod = 1,
		.cali = ULPOSC_CAL_START_VALUE,
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

	/* Enable CP */
	val |= OSC_CP_EN;
	/* set div */
	val |= opp->div << OSC_DIV_SHIFT;
	/* set I-band */
	val |= (opp->iband << OSC_IBAND_SHIFT);
	/* set calibration */
	val |= opp->cali;
	/* set control register */
	AP_ULPOSC_CON02(opp->osc) = val;

	/* OSC_DIV2_EN = 1 */
	AP_ULPOSC_CON13(opp->osc) |= OSC_DIV2_EN;
	/* OSC_MOD = 00 */
	AP_ULPOSC_CON13(opp->osc) &= ~OSC_MOD_MASK;
}

static void clock_ulposc_config_cali(struct opp_ulposc_cfg *opp,
				     uint32_t cali_val)
{
	uint32_t val;

	val = AP_ULPOSC_CON02(opp->osc);
	val &= ~OSC_CALI_MASK;
	val |= cali_val;
	AP_ULPOSC_CON02(opp->osc) = val;

	clock_busy_udelay(50);
}

static unsigned int clock_ulposc_measure_freq(int osc)
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
	 * as clock_busy_udelay may not be accurate when sysclk is not 26Mhz
	 * (e.g. when recalibrating/measuring after boot).
	 */
	for (cnt = 100; cnt; cnt--) {
		clock_busy_udelay(1);
		if (!(AP_SCP_CFG_0 & CFG_FREQ_METER_RUN)) {
			result = CFG_FREQ_COUNTER(AP_SCP_CFG_1);
			break;
		}
	}

	/* Disable freq meter */
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
	/* Enable high speed clock */
	SCP_CLK_EN |= EN_CLK_HIGH;

	switch (osc) {
	case 0:
		/* After 25ms, enable ULPOSC */
		clock_busy_udelay(25 * MSEC);
		SCP_CLK_EN |= CG_CLK_HIGH;
		break;
	case 1:
		/* Turn off ULPOSC2 high-core-disable switch */
		SCP_CLK_ON_CTRL &= ~HIGH_CORE_DIS_SUB;
		/* After 25ms, turn on ULPOSC2 high core clock gate */
		clock_busy_udelay(25 * MSEC);
		SCP_CLK_HIGH_CORE |= CLK_HIGH_CORE_CG;
		break;
	default:
		break;
	}
	clock_busy_udelay(25 * MSEC);
}

static void clock_calibrate_ulposc(struct opp_ulposc_cfg *opp)
{
	/*
	 * TODO: Check ULPOSC1(osc=0) is already ?
	 * - calibrated
	 * - enabled in coreboot
	 * - used by pmic wrapper
	 */

	clock_ulposc_config_default(opp);
	clock_high_enable(opp->osc);


	/* Calibrate only if it is not accurate enough. */
	if (!clock_ulposc_is_calibrated(opp))
		opp->cali = clock_ulposc_process_cali(opp);

	CPRINTF("osc:%u, target=%uMHz, cal:%u\n",
		opp->osc, opp->target_mhz, opp->cali);
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

void clock_init(void)
{
	int i;

	/* Select default CPU clock */
	scp_use_clock(SCP_CLK_26M);

	/* VREQ */
	SCP_CPU_VREQ = VREQ_SEL | VREQ_DVFS_SEL;
	SCP_SECURE_CTRL |= ENABLE_SPM_MASK_VREQ;
	SCP_CLK_CTRL_GENERAL_CTRL &= ~VREQ_PMIC_WRAP_SEL;

	/* DDREN auto mode */
	SCP_SYS_CTRL |= AUTO_DDREN;

	/* Set settle time */
	SCP_CLK_SYS_VAL =
		(SCP_CLK_SYS_VAL & ~CLK_SYS_VAL_MASK) | CLK_SYS_VAL(1);
	SCP_CLK_HIGH_VAL =
		(SCP_CLK_HIGH_VAL & ~CLK_HIGH_VAL_MASK) | CLK_HIGH_VAL(1);
	SCP_CLK_SLEEP_CTRL =
		(SCP_CLK_SLEEP_CTRL & ~VREQ_COUNTER_MASK) | VREQ_COUNTER_VAL(1);

	/* Set RG MUX to SW mode */
	AP_PLL_CON0 = LTECLKSQ_EN | LTECLKSQ_LPF_EN | LTECLKSQ_HYS_EN | LTECLKSQ_VOD_EN |
		LTECLKSQ_HYS_SEL | CLKSQ_RESERVE | SSUSB26M_CK2_EN | SSUSB26M_CK_EN|
		XTAL26M_CK_EN | ULPOSC_CTRL_SEL;

	/* Turn off ULPOSC2 */
	SCP_CLK_ON_CTRL |= HIGH_CORE_DIS_SUB;

	/* calibrate ULPOSC1 & ULPOSC2 */
	for (i = 0; i < ARRAY_SIZE(opp); ++i)
		clock_calibrate_ulposc(&opp[i]);

	/* Select ULPOSC2 high speed CPU clock */
	scp_use_clock(SCP_CLK_ULPOSC2);

	/* Enable default clock gate */
	SCP_CLK_GATE |= CG_DMA_CH3 | CG_DMA_CH2 | CG_DMA_CH1 | CG_DMA_CH0 |
			CG_I2C_M | CG_MAD_M | CG_AP2P_M;

	task_enable_irq(SCP_IRQ_CLOCK);
	task_enable_irq(SCP_IRQ_CLOCK2);
}

DECLARE_IRQ(SCP_IRQ_CLOCK, clock_control_irq, 3);
void clock_control_irq(void)
{
	/* Read ack CLK_IRQ */
	(SCP_CLK_IRQ_ACK);
	task_clear_pending_irq(SCP_IRQ_CLOCK);
}

DECLARE_IRQ(SCP_IRQ_CLOCK2, clock_fast_wakeup_irq, 3);
void clock_fast_wakeup_irq(void)
{
	/* Ack fast wakeup */
	SCP_SLEEP_IRQ2 = 1;
	task_clear_pending_irq(SCP_IRQ_CLOCK2);
}

/* Console command */
static int command_ulposc(int argc, char *argv[])
{
	int i;

	for (i = 0; i <= 1; ++i)
		ccprintf("ULPOSC%u frequency: %u kHz\n",
			 i + 1,
			 clock_ulposc_measure_freq(i) * 26 * 1000 / 1024);
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(ulposc, command_ulposc, "[calibrate]",
			"Calibrate ULPOSC frequency");
