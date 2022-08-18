/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Clocks, PLL and power settings */

#include "clock.h"
#include "clock_s3.h"
#include "common.h"
#include "console.h"
#include "csr.h"
#include "ec_commands.h"
#include "power.h"
#include "registers.h"
#include "task.h"

#include <assert.h>
#include <string.h>

#define CPRINTF(format, args...) cprintf(CC_CLOCK, format, ##args)
#define CPRINTS(format, args...) cprints(CC_CLOCK, format, ##args)

#ifdef BOARD_GERALT_SCP_CORE1

void clock_init(void)
{
	/* clock is controlled by core 0 */
	return;
}

#else

enum {
	OPP_ULPOSC2_LOW_SPEED,
	OPP_ULPOSC2_HIGH_SPEED,
};

static struct opp_ulposc_cfg {
	uint32_t osc;
	uint32_t div;
	uint32_t fband;
	uint32_t mod;
	uint32_t cali;
	uint32_t target_mhz;
	uint32_t clk_div;
} opp[] = {
	[OPP_ULPOSC2_LOW_SPEED] = {
		.osc = 1, .target_mhz = 250, .clk_div = CLK_DIV_SEL2, .div = 13,
		.fband = 2, .mod = 0, .cali = 64, /* 250MHz */
	},
	[OPP_ULPOSC2_HIGH_SPEED] = {
		.osc = 1, .target_mhz = 400, .clk_div = CLK_DIV_SEL1, .div = 22,
		.fband = 10, .mod = 0, .cali = 64, /* 400MHz */
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
	uint32_t val = 0;

	/* set div, cp_en = 0 */
	val |= opp->div << OSC_DIV_SHIFT;
	/* set F-band, I-band = 82 */
	val |= (opp->fband << OSC_FBAND_SHIFT) | (82 << OSC_IBAND_SHIFT);
	/* set calibration */
	val |= opp->cali;
	/* set control register 0 */
	AP_ULPOSC_CON0(opp->osc) = val;

	clock_busy_udelay(50);

	/* set mod, div2_en = 0, cp_en = 0 */
	val = opp->mod << OSC_MOD_SHIFT;
	/* rsv2 = 0, rsv1 = 41, cali_32k = 0 */
	val |= 41 << OSC_RSV1_SHIFT;
	/* set control register 1 */
	AP_ULPOSC_CON1(opp->osc) = val;

	/* bias = 65 */
	val = 0x41 << OSC_BIAS_SHIFT;
	AP_ULPOSC_CON2(opp->osc) = val;

	/* set settle time */
	SCP_CLK_HIGH_VAL = (SCP_CLK_HIGH_VAL & ~CLK_HIGH_VAL_MASK) |
			   CLK_HIGH_VAL_VAL(2);
}

static void clock_ulposc_config_cali(struct opp_ulposc_cfg *opp,
				     uint32_t cali_val)
{
	uint32_t val;

	val = AP_ULPOSC_CON0(opp->osc);
	val &= ~OSC_CALI_MASK;
	val |= cali_val;
	AP_ULPOSC_CON0(opp->osc) = val;
	opp->cali = cali_val;

	clock_busy_udelay(50);
}

static uint32_t clock_ulposc_measure_freq(uint32_t osc)
{
	uint32_t result = 0;
	int cnt;
	uint32_t cali_0 = AP_CLK26CALI_0;
	uint32_t cali_1 = AP_CLK26CALI_1;
	uint32_t dbg_cfg = AP_CLK_DBG_CFG;

	/* Set ckgen_load_cnt: CLK26CALI_1[25:16] */
	AP_CLK26CALI_1 = CFG_CKGEN_LOAD_CNT;

	/* select monclk_ext2fqmtr_sel: AP_CLK_DBG_CFG[14:8] */
	AP_CLK_DBG_CFG =
		(osc == 0 ? DBG_BIST_SOURCE_ULPOSC1 : DBG_BIST_SOURCE_ULPOSC2);

	/* enable frequency meter, without start */
	AP_CLK26CALI_0 = CFG_FREQ_METER_ENABLE;

	clock_busy_udelay(1);

	/* trigger frequency meter start */
	AP_CLK26CALI_0 |= CFG_FREQ_METER_RUN;

	clock_busy_udelay(45);

	for (cnt = 10000; cnt > 0; --cnt) {
		clock_busy_udelay(10);
		if (!(AP_CLK26CALI_0 & CFG_FREQ_METER_RUN)) {
			result = CFG_FREQ_COUNTER(AP_CLK26CALI_1);
			break;
		}
	}

	AP_CLK26CALI_0 = cali_0;
	AP_CLK26CALI_1 = cali_1;
	AP_CLK_DBG_CFG = dbg_cfg;

	/* disable freq meter */
	AP_CLK26CALI_0 &= ~CFG_FREQ_METER_ENABLE;

	return result;
}

/* The HW tolerates +/-4%. Use +/-2% to reserve margin for temperature
 * variation. The valid freq. ranges are, 400:(392,408) 250:(245,255)
 */
#define CAL_MIS_RATE 20
static int clock_ulposc_is_calibrated(struct opp_ulposc_cfg *opp)
{
	uint32_t curr, target;

	curr = clock_ulposc_measure_freq(opp->osc);
	target = opp->target_mhz * 512 / 26;

#ifdef DEBUG
	CPRINTF("osc:%u, target=%uMHz, curr=%uMHz, cali:%u\n", opp->osc,
		opp->target_mhz, (curr * 26) / 512, opp->cali);
#endif

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
	uint32_t target_val = opp->target_mhz * 512 / 26;
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
		/* topck ulposc1 clk gating off */
		AP_CLK_CFG_22_CLR = PDN_F_ULPOSC_CK;
		/* select topck ulposc1 as scp clk parent */
		AP_CLK_CFG_22_CLR = ULPOSC_CLK_SEL;

		AP_CLK_CFG_UPDATE2 = F_ULPOSC_CK_UPDATE;
		clock_busy_udelay(50);

		/* after 150us, enable ULPOSC */
		clock_busy_udelay(150);

		/* wait clock ack signal back */
		while (!(SCP_CLK_SAFE_ACK & CLK_SAFE_ACK_HIGH))
			;

		SCP_CLK_ENABLE |= CLK_HIGH_CG;
		clock_busy_udelay(50);
		break;
	case 1:
		/* topck ulposc2 clk gating off */
		AP_CLK_MISC_CFG_1 |= F_ULPOSC_CORE_CK_EN;
		clock_busy_udelay(50);

		/* turn off ULPOSC2 high-core-disable switch */
		SCP_CLK_ON_CTRL &= ~HIGH_CORE_DIS_SUB;

		/* after 150us, scp requests ULPOSC2 high core clock */
		clock_busy_udelay(150);

		/* wait clock ack signal back */
		while (!(SCP_CLK_SAFE_ACK & CLK_SAFE_ACK_HIGH))
			;

		SCP_CLK_ENABLE |= CLK_HIGH_CG;
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
		/* scp releases ulposc1 clk */
		SCP_CLK_ENABLE &= ~CLK_HIGH_CG;
		clock_busy_udelay(50);
		SCP_CLK_ENABLE &= ~CLK_HIGH_EN;
		clock_busy_udelay(50);

		/* topck ulposc1 clk gating on */
		AP_CLK_CFG_22_SET = PDN_F_ULPOSC_CK;
		AP_CLK_CFG_UPDATE2 = F_ULPOSC_CK_UPDATE;
		clock_busy_udelay(50);
		break;
	case 1:
		/* scp releases ulposc2 clk */
		SCP_CLK_HIGH_CORE_CG &= ~HIGH_CORE_CG;
		clock_busy_udelay(50);
		SCP_CLK_ON_CTRL |= HIGH_CORE_DIS_SUB;
		clock_busy_udelay(50);

		/* topck ulposc2 clk gating on */
		AP_CLK_MISC_CFG_1 &= ~F_ULPOSC_CORE_CK_EN;
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
	if (!clock_ulposc_is_calibrated(opp)) {
		opp->cali = clock_ulposc_process_cali(opp);
	}
}

void clock_select_clock(enum scp_clock_source src)
{
	uint32_t sel;
	uint32_t div;

	switch (src) {
	case SCP_CLK_SYSTEM:
		div = CLK_DIV_SEL1;
		sel = CLK_SW_SEL_SYSTEM;
		break;
	case SCP_CLK_32K:
		div = CLK_DIV_SEL1;
		sel = CLK_SW_SEL_32K;
		break;
	case SCP_CLK_ULPOSC1:
		div = CLK_DIV_SEL1;
		sel = CLK_SW_SEL_ULPOSC1;
		break;
	case SCP_CLK_ULPOSC2_LOW_SPEED:
		/* parking at scp system clk until ulposc clk is ready */
		clock_select_clock(SCP_CLK_SYSTEM);

		clock_ulposc_config_cali(&opp[OPP_ULPOSC2_LOW_SPEED],
					 opp[OPP_ULPOSC2_LOW_SPEED].cali);
		div = opp[OPP_ULPOSC2_LOW_SPEED].clk_div;

		sel = CLK_SW_SEL_ULPOSC2;
		break;
	case SCP_CLK_ULPOSC2_HIGH_SPEED:
		/* parking at scp system clk until ulposc clk is ready */
		clock_select_clock(SCP_CLK_SYSTEM);

		clock_ulposc_config_cali(&opp[OPP_ULPOSC2_HIGH_SPEED],
					 opp[OPP_ULPOSC2_HIGH_SPEED].cali);
		div = opp[OPP_ULPOSC2_HIGH_SPEED].clk_div;

		sel = CLK_SW_SEL_ULPOSC2;
		break;
	default:
		div = CLK_DIV_SEL1;
		sel = CLK_SW_SEL_SYSTEM;
		break;
	}

	SCP_CLK_DIV_SEL = div;
	SCP_CLK_SW_SEL = sel;
}

__override void
power_chipset_handle_host_sleep_event(enum host_sleep_event state,
				      struct host_sleep_event_context *ctx)
{
	if (state == HOST_SLEEP_EVENT_S3_SUSPEND) {
		CPRINTS("AP suspend");
		clock_select_clock(SCP_CLK_ULPOSC2_LOW_SPEED);
#ifdef HAS_TASK_SR
		task_set_event(TASK_ID_SR, TASK_EVENT_SUSPEND);
#endif
	} else if (state == HOST_SLEEP_EVENT_S3_RESUME) {
#ifdef HAS_TASK_SR
		task_set_event(TASK_ID_SR, TASK_EVENT_RESUME);
#endif
		clock_select_clock(SCP_CLK_ULPOSC2_HIGH_SPEED);
		CPRINTS("AP resume");
	}
}

void clock_init(void)
{
	uint32_t i;

	/* select scp system clock (default 26MHz) */
	clock_select_clock(SCP_CLK_SYSTEM);

	/* set VREQ to HW mode */
	SCP_CPU_VREQ_CTRL = VREQ_SEL | VREQ_DVFS_SEL;
	SCP_CLK_CTRL_GENERAL_CTRL &= ~VREQ_PMIC_WRAP_SEL;
	SCP_SEC_CTRL &= ~VREQ_SECURE_DIS;

	/* set DDREN to auto mode */
	SCP_SYS_CTRL |= AUTO_DDREN;

	/* set settle time */
	SCP_CLK_SYS_VAL = (SCP_CLK_SYS_VAL & ~CLK_SYS_VAL_MASK) |
			  CLK_SYS_VAL_VAL(1);
	SCP_CLK_HIGH_VAL = (SCP_CLK_HIGH_VAL & ~CLK_HIGH_VAL_MASK) |
			   CLK_HIGH_VAL_VAL(1);
	SCP_SLEEP_CTRL = (SCP_SLEEP_CTRL & ~VREQ_COUNT_MASK) |
			 VREQ_COUNT_VAL(1);

	/* turn off ULPOSC2 */
	SCP_CLK_ON_CTRL |= HIGH_CORE_DIS_SUB;

	/* calibrate ULPOSC2 */
	for (i = 0; i < ARRAY_SIZE(opp); ++i)
		clock_calibrate_ulposc(&opp[i]);

	/* select ULPOSC2 high speed SCP clock */
	clock_select_clock(SCP_CLK_ULPOSC2_HIGH_SPEED);

	/* select BCLK to use ULPOSC / 8 */
	SCP_BCLK_CK_SEL = BCLK_CK_SEL_ULPOSC_DIV8;

	/* enable default clock gate */
	SCP_SET_CLK_CG |= CG_DMA_CH3 | CG_DMA_CH2 | CG_DMA_CH1 | CG_DMA_CH0 |
			  CG_I2C_MCLK | CG_MAD_MCLK | CG_AP2P_MCLK;
}

#ifdef DEBUG
static int command_ulposc(int argc, char *argv[])
{
	uint32_t osc;

	for (osc = 0; osc <= OPP_ULPOSC2_HIGH_SPEED; ++osc)
		ccprintf("ULPOSC%u frequency: %u kHz\n", osc + 1,
			 clock_ulposc_measure_freq(osc) * 26 * 1000 / 512);

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(ulposc, command_ulposc, "[ulposc]",
			"Measure ULPOSC frequency");
#endif
#endif /* BOARD_GERALT_SCP_CORE1 */
