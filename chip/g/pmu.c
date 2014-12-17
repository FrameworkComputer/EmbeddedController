/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "pmu.h"
#include "task.h"

/*
 * RC Trim constants
 */
#define RCTRIM_RESOLUTION       (12)
#define RCTRIM_LOAD_VAL	        (1 << 11)
#define RCTRIM_RANGE_MAX	(7 * 7)
#define RCTRIM_RANGE_MIN	(-8 * 7)
#define RCTRIM_RANGE		(RCTRIM_RANGE_MAX - RCTRIM_RANGE_MIN + 1)

/*
 * Enable peripheral clock
 * @param perih Peripheral from @ref uint32_t
 */
void pmu_clock_en(uint32_t periph)
{
	if (periph <= 31)
		GR_PMU_PERICLKSET0 = (1 << periph);
	else
		GR_PMU_PERICLKSET1 = (1 << (periph - 32));
}

/*
 * Disable peripheral clock
 * @param perih Peripheral from @ref uint32_t
 */
void pmu_clock_dis(uint32_t periph)
{
	if (periph <= 31)
		GR_PMU_PERICLKCLR0 = (1 << periph);
	else
		GR_PMU_PERICLKCLR1 = (1 << (periph - 32));
}

/*
 * Peripheral reset
 * @param periph Peripheral from @ref uint32_t
 */
void pmu_peripheral_rst(uint32_t periph)
{
	/* Reset high */
	if (periph <= 31)
		GR_PMU_RST0 = 1 << periph;
	else
		GR_PMU_RST1 = 1 << (periph - 32);
}

/*
 * Internal helper to convert value to trim code
 */
static uint32_t _pmu_value_to_trim(uint32_t val)
{
	uint32_t base = val / 7;
	uint32_t mod  = val % 7;
	uint32_t code = 0x0;
	uint32_t digit;

	/* Increasing count from right to left */
	for (digit = 0; digit < 7; digit++) {
		if (digit < mod)
			code |= (((base + 1) & 0xF) << (4 * digit));
		else
			code |= ((base & 0xF) << (4 * digit));
	}

	return code;
}

/*
 * Run the RC calibration counters
 * This must be used otherwise the counters may get stuck
 */
static uint32_t _pmu_run_rc_counters(uint32_t load_val, uint32_t trim_val)
{
	uint32_t trim_code;

	/* Convert value to trim code */
	trim_code = _pmu_value_to_trim(trim_val);

	/* Set the trim value */
	GWRITE_FIELD(XO, OSC_RC, TRIM, trim_code);

	/* Reset counters */
	GR_XO_OSC_RC_CAL_RSTB = 0x0;
	GR_XO_OSC_RC_CAL_RSTB = 0x1;

	/* Load */
	GR_XO_OSC_RC_CAL_LOAD = load_val;

	/* Do calibration */
	GR_XO_OSC_RC_CAL_START = 0x1;

	/*
	* There is a small race condition because of the delay in dregfile.
	* The start doesn't actually appear for 2 clock cycles after the write.
	* So, poll until done goes low.
	*/
	while (GR_XO_OSC_RC_CAL_DONE)
		;

	/* Wait until it's done */
	while (!GR_XO_OSC_RC_CAL_DONE)
		;

	/* Calculate the difference */
	return GR_XO_OSC_RC_CAL_LOAD - GR_XO_OSC_RC_CAL_COUNT;
}

/*
 * Calibrate RC trim
 */
uint32_t pmu_calibrate_rc_trim(void)
{
	uint32_t size, iter;
	uint32_t mid;
	uint32_t diff;

	/*
	* Switch to crystal for calibration
	* This should work since we are on an uncalibrated RC trim clock
	*/
	pmu_clock_switch_xo();

	/* Clear the HOLD signal on dxo */
	GR_XO_OSC_CLRHOLD = GC_XO_OSC_CLRHOLD_RC_TRIM_MASK;

	/* Clear EN bit while iterating through codes */
	GWRITE_FIELD(XO, OSC_RC, EN, 0);

	/* Begin binary search */
	mid = RCTRIM_RANGE_MAX - (RCTRIM_RANGE / 2);
	size = RCTRIM_RANGE / 2;
	for (iter = 0; iter < 8; iter++) {
		/* Run the counters */
		diff = _pmu_run_rc_counters(RCTRIM_LOAD_VAL, mid);

		/*
		 * Test to see whether we are still outside of
		 * our desired resolution
		 */
		if ((diff < -RCTRIM_RESOLUTION)
			|| (diff > RCTRIM_RESOLUTION)) {
			if (diff > 0)
				mid -= size / 2;
			else
				mid += size / 2;
		}

		/* Move to next range, round up */
		size = (size + 1) >> 1;
	}

	/* Set the final trim value, set EN bit to lock in the code */
	GR_XO_OSC_RC = (_pmu_value_to_trim(mid) << GC_XO_OSC_RC_TRIM_LSB)
			| (0x1 << GC_XO_OSC_RC_EN_LSB);

	/* Set EN bit to lock in this trim_code */
	/* GWRITE_FIELD(XO, OSC_RC, EN, 1); */

	/* Set the HOLD signal on dxo */
	GR_XO_OSC_SETHOLD = GC_XO_OSC_SETHOLD_RC_TRIM_MASK;

	/* Switch back to the RC trim now that we are calibrated */
	/* pmu_clock_switch_rc_trim(); */

	return _pmu_value_to_trim(mid);
}

/*
 * Switch system clock to RC no trim
 */
uint32_t pmu_clock_switch_rc_notrim(void)
{
	uint32_t osc_sel;

	/* check which clock we are running on */
	osc_sel = GR_PMU_OSC_SELECT_STAT;

	if (osc_sel == GC_PMU_OSC_SELECT_RC) {
		/* Already on untrimmed RC */
		return 0;
	} else if (osc_sel == GC_PMU_OSC_SELECT_XTL) {
		/* Need to switch to RC trimmed first */
		pmu_clock_switch_rc_trim(1);
	}

	/* Turn on XO clock */
	pmu_clock_en(PERIPH_XO);

	/* Power up RC notrim clock if it's currently off */
	GWRITE_FIELD(PMU, CLRDIS, RC_NOTRIM, 1);

	/* Switch to the clock */
	GR_PMU_OSC_HOLD_CLR = 0x1; /* make sure the hold signal is clear */
	GR_PMU_OSC_SELECT   = GC_PMU_OSC_SELECT_RC;
	/* make sure the hold signal is set for future power downs */
	GR_PMU_OSC_HOLD_SET = 0x1;

	return 0;
}

/*
 * enable clock doubler for USB purposes
 */
void pmu_enable_clock_doubler(void)
{
	/* enable stuff */
	GREG32(XO, OSC_ADC_CAL_FREQ2X) =
		(GC_XO_OSC_ADC_CAL_FREQ2X_CNTL_DEFAULT
			<< GC_XO_OSC_ADC_CAL_FREQ2X_CNTL_LSB) |
		(1 << GC_XO_OSC_ADC_CAL_FREQ2X_EN_LSB);
	/* enable more stuff */
	GREG32(XO, OSC_CLKOUT) =
		(1 << GC_XO_OSC_CLKOUT_ADC_EN_LSB)  |
		(1 << GC_XO_OSC_CLKOUT_PLL_EN_LSB)  |
		(1 << GC_XO_OSC_CLKOUT_BADC_EN_LSB) |
		(1 << GC_XO_OSC_CLKOUT_USB_EN_LSB);

	/* make sure doubled clock is selected */
	GREG32(XO, OSC_24_48B_SEL) = GC_XO_OSC_24_48B_SEL_DEFAULT;
}

/*
 * Switch system clock to RC trim
 */
uint32_t pmu_clock_switch_rc_trim(uint32_t skip_calibration)
{
	uint32_t trimmed;
	uint32_t trim_code;
	uint32_t osc_sel;

	/* check which clock we are running on */
	osc_sel = GREG32(PMU, OSC_SELECT_STAT);

	if (osc_sel == GC_PMU_OSC_SELECT_RC_TRIM) {
		/*
		 * already using the rc_trim so nothing to do here
		 * make sure the hold signal is set for future power downs
		 */
		GREG32(PMU, OSC_HOLD_SET) = 0x1;
		return 0;
	}

	/* Turn on DXO clock so we can write in the trim code in */
	pmu_clock_en(PERIPH_XO);

	/* Disable the RC Trim flops in the glitchless switch */
	GWRITE_FIELD(PMU, OSC_CTRL, RC_TRIM_READYB, 0x1);

	/* Power up the clock if not already powered up */
	GREG32(PMU, CLRDIS) = 1 << GC_PMU_SETDIS_RC_TRIM_LSB;

	/* Check for the trim code in the always-on domain
	 * before looking at the fuse
	 */
	if (GREAD_FIELD(XO, OSC_RC_STATUS, EN)) {
		trim_code = GREAD_FIELD(XO, OSC_RC_STATUS, TRIM);
		trimmed = 1;
	} else if (GREAD_FIELD(PMU, FUSE_RD_RC_OSC_26MHZ, EN)) {
		trim_code = GREAD_FIELD(PMU, FUSE_RD_RC_OSC_26MHZ, TRIM);
		trimmed = 1;
	} else {
		if (skip_calibration) {
			trim_code = GREAD_FIELD(XO, OSC_RC, TRIM);
			trimmed = 0;
		} else {
			trim_code = pmu_calibrate_rc_trim();
			trimmed = 1;
		}
	}

	/* Write the trim code to dxo */
	if (trimmed) {
		/* clear the hold signal */
		GREG32(XO, OSC_CLRHOLD) = GC_XO_OSC_CLRHOLD_RC_TRIM_MASK;

		/* Write the trim code and enable the trim code */
		GREG32(XO, OSC_RC) = (trim_code << GC_XO_OSC_RC_TRIM_LSB) |
					(1 << GC_XO_OSC_RC_EN_LSB);

		/* set the hold signal */
		GREG32(XO, OSC_SETHOLD) = 1 << GC_XO_OSC_SETHOLD_RC_TRIM_LSB;
	}

	/* Enable the flops for RC TRIM in the glitchless switch */
	GWRITE_FIELD(PMU, OSC_CTRL, RC_TRIM_READYB, 0x0);

	/*
	* Switch the select signal
	* make sure the hold signal is clear
	*/
	GREG32(PMU, OSC_HOLD_CLR) = 0x1;
	GREG32(PMU, OSC_SELECT)   = GC_PMU_OSC_SELECT_RC_TRIM;

	/* make sure the hold signal is set for future power downs */
	GREG32(PMU, OSC_HOLD_SET) = 0x1;

	return !trimmed;
}

/*
 * Switch system clock to XO
 * @returns The value of XO_OSC_XTL_FSM_STATUS.	0 = okay, 1 = error.
 */
uint32_t pmu_clock_switch_xo(void)
{
	uint32_t osc_sel;
	uint32_t trim_code, final_trim, fsm_done, fsm_status;

	/* check which clock we are running on */
	osc_sel = GREG32(PMU, OSC_SELECT_STAT);

	if (osc_sel == GC_PMU_OSC_SELECT_XTL) {
		/*
		 * already using the crystal so nothing to do here
		 * make sure the hold signal is set for future power downs
		 */
		GREG32(PMU, OSC_HOLD_SET) = 0x1;
		return 0;
	} else if (osc_sel == GC_PMU_OSC_SELECT_RC) {
		/*
		 * RC untrimmed clock. We must go through
		 * the trimmed clock first to avoid glitching
		 */
		pmu_clock_switch_rc_trim(1);
	}

	/* Turn on DXO clock so we can write in the trim code in */
	pmu_clock_en(PERIPH_XO);

	/* Disable the XTL Clock */
	GWRITE_FIELD(PMU, OSC_CTRL, XTL_READYB, 0x1);

	/* Power up the clock if not already powered up */
	GREG32(PMU, CLRDIS) = 1 << GC_PMU_CLRDIS_XTL_LSB;

	/* Try to find the trim code */
	trim_code = 0;

	/*
	* Check for the trim code in the always-on domain
	* before looking at the fuse
	*/
	if (GREAD_FIELD(XO, OSC_XTL_TRIM_STAT, EN)) {
		/* nothing to do */
		trim_code = GREAD_FIELD(XO, OSC_XTL_TRIM_STAT, CODE);

	} else if (GREAD_FIELD(PMU, FUSE_RD_XTL_OSC_26MHZ, EN)) {

		/* push the fuse trim code as the saved trim code */
		trim_code = GREAD_FIELD(PMU, FUSE_RD_XTL_OSC_26MHZ, TRIM);

		/* make sure the hold signal is clear */
		GREG32(XO, OSC_CLRHOLD)  = 1 << GC_XO_OSC_CLRHOLD_XTL_LSB;
		GREG32(XO, OSC_XTL_TRIM) =
			(trim_code << GC_XO_OSC_XTL_TRIM_CODE_LSB)
				| (0x1 << GC_XO_OSC_XTL_TRIM_EN_LSB);
	} else {
		/* Run the crystal FSM to calibrate the crystal trim */
		fsm_done = GREG32(XO, OSC_XTL_FSM);
		if (fsm_done & GC_XO_OSC_XTL_FSM_DONE_MASK) {
			/*
			 * If FSM done is high, it means we already ran it
			 * so let's not run it again
			 * DO NOTHING
			 */
		} else {
			/* reset FSM */
			GREG32(XO, OSC_XTL_FSM_EN) = 0x0;
			GREG32(XO, OSC_XTL_FSM_EN) = GC_XO_OSC_XTL_FSM_EN_KEY;
			while (!(fsm_done & GC_XO_OSC_XTL_FSM_DONE_MASK))
				fsm_done = GREG32(XO, OSC_XTL_FSM);
		}
	}

	/* Check the status and final trim value */
	/* max_trim = GREAD_FIELD(XO, OSC_XTL_FSM_CFG, TRIM_MAX); */
	final_trim = GREAD_FIELD(XO, OSC_XTL_FSM, TRIM);
	fsm_status = GREAD_FIELD(XO, OSC_XTL_FSM, STATUS);

	/*
	 * Save the trim for future powerups
	 * make sure the hold signal is clear (may have already been cleared)
	 */
	GREG32(XO, OSC_CLRHOLD)  = 1 << GC_XO_OSC_CLRHOLD_XTL_LSB;
	GREG32(XO, OSC_XTL_TRIM) = (final_trim << GC_XO_OSC_XTL_TRIM_CODE_LSB) |
				 (1 << GC_XO_OSC_XTL_TRIM_EN_LSB);

	/* make sure the hold signal is set for future power downs */
	GREG32(XO, OSC_SETHOLD)  = 1 << GC_XO_OSC_SETHOLD_XTL_LSB;

	/* Enable the flops for XTL in the glitchless switch */
	GWRITE_FIELD(PMU, OSC_CTRL, XTL_READYB, 0x0);

	/*
	* Switch the select signal
	* make sure the hold signal is clear
	*/
	GREG32(PMU, OSC_HOLD_CLR) = 0x1;
	GREG32(PMU, OSC_SELECT)   = GC_PMU_OSC_SELECT_XTL;

	/* make sure the hold signal is set for future power downs */
	GREG32(PMU, OSC_HOLD_SET) = 0x1;

	return !fsm_status;
}

/*
 * Enter sleep mode and handle exiting from sleep mode
 * @warning The CPU must be in RC no trim mode before calling this function
 */
void pmu_sleep(void)
{
	uint32_t val;

	/* Enable PMU sleep interrupts */
	GREG32(PMU, ICTRL) = 1 << GC_PMU_ICTRL_SLEEP_LSB;

	/* nvic_irq_en(GC_IRQNUM_PMU_PMUINT); */

	/* Enable CPU SLEEPDEEP */
	val = GREG32(M3, SCR);
	GREG32(M3, SCR) = val | 0x4;

	/* Enable WIC mode */
	GREG32(PMU, SETWIC) = 1 << GC_PMU_SETWIC_PROC0_LSB;

	/* Disable power domains for entering sleep mode */
	GREG32(PMU, SETDIS) = (1 << GC_PMU_SETDIS_START_LSB)	|
			(1 << GC_PMU_SETDIS_VDDL_LSB)		|
			(1 << GC_PMU_SETDIS_VDDA_LSB)		|
			(1 << GC_PMU_SETDIS_VDDSRM_LSB)		|
			(1 << GC_PMU_SETDIS_BGAP_LSB)		|
			(1 << GC_PMU_SETDIS_VDDXO_LSB)		|
			(1 << GC_PMU_SETDIS_VDDXOLP_LSB)	|
			(1 << GC_PMU_SETDIS_XTL_LSB)		|
			(1 << GC_PMU_SETDIS_RC_TRIM_LSB)	|
			(1 << GC_PMU_SETDIS_RC_NOTRIM_LSB)	|
			(1 << GC_PMU_SETDIS_BATMON_LSB)		|
			(1 << GC_PMU_SETDIS_FST_BRNOUT_PWR_LSB)	|
			(1 << GC_PMU_SETDIS_FST_BRNOUT_LSB);

	/* Wait for exit interrupt
	 * @todo Add code to disable all non-PMU interrupts.
	 */
	__asm__("wfi");

	/* Disable WIC mode */
	GREG32(PMU, CLRWIC) = 1 << GC_PMU_CLRWIC_PROC0_LSB;

	/* Disable CPU SLEEPDEEP */
	val = GREG32(M3, SCR);
	GREG32(M3, SCR) = val & (~0x4);

	/* Re-enable power domains */
	GREG32(PMU, CLRDIS) = (1 << GC_PMU_CLRDIS_START_LSB)	|
			(1 << GC_PMU_CLRDIS_VDDL_LSB)		|
			(1 << GC_PMU_CLRDIS_VDDA_LSB)		|
			(1 << GC_PMU_CLRDIS_VDDSRM_LSB)		|
			(1 << GC_PMU_CLRDIS_VDDIOF_LSB)		|
			(1 << GC_PMU_CLRDIS_BGAP_LSB)		|
			(1 << GC_PMU_CLRDIS_VDDXO_LSB);

#ifdef __FIX_ME__
	GREG32(PMU, CLRDIS) = (1 << GC_PMU_CLRDIS_START_LSB)	|
			(1 << GC_PMU_CLRDIS_VDDL_LSB)		|
			(1 << GC_PMU_CLRDIS_VDDA_LSB)		|
			(1 << GC_PMU_CLRDIS_VDDSRM_LSB)		|
			(1 << GC_PMU_CLRDIS_BGAP_LSB)		|
			(1 << GC_PMU_CLRDIS_VDDXO_LSB)		|
			(1 << GC_PMU_CLRDIS_VDDXOLP_LSB)	|
			(1 << GC_PMU_CLRDIS_XTL_LSB)		|
			(1 << GC_PMU_CLRDIS_RC_TRIM_LSB)	|
			(1 << GC_PMU_CLRDIS_RC_NOTRIM_LSB)	|
			(1 << GC_PMU_CLRDIS_BATMON_LSB)		|
			(1 << GC_PMU_CLRDIS_FST_BRNOUT_PWR_LSB)	|
			(1 << GC_PMU_CLRDIS_FST_BRNOUT_LSB);
#endif
}

/*
 * Enter hibernate mode
 * This function does not return. The powerdown exit event will
 * cause the CPU to begin executing the system / app bootloader.
 * @warning The CPU must be in RC no trim mode
 */
void pmu_hibernate(void)
{
	/* Turn off power to everything except retention domains */
	GREG32(PMU, SETDIS) = (1 << GC_PMU_SETDIS_START_LSB)	|
			(1 << GC_PMU_SETDIS_VDDL_LSB)		|
			(1 << GC_PMU_SETDIS_VDDA_LSB)		|
			(1 << GC_PMU_SETDIS_VDDSRM_LSB)		|
			(1 << GC_PMU_SETDIS_VDDIOF_LSB)		|
			(1 << GC_PMU_SETDIS_VDDLK_LSB)		|
			(1 << GC_PMU_SETDIS_VDDSK_LSB)		|
			(1 << GC_PMU_SETDIS_BIAS_LSB)		|
			(1 << GC_PMU_SETDIS_BGAP_LSB)		|
			(1 << GC_PMU_SETDIS_VDDXO_LSB)		|
			(1 << GC_PMU_SETDIS_VDDXOLP_LSB)	|
			(1 << GC_PMU_SETDIS_XTL_LSB)		|
			(1 << GC_PMU_SETDIS_RC_TRIM_LSB)	|
			(1 << GC_PMU_SETDIS_RC_NOTRIM_LSB)	|
			(1 << GC_PMU_SETDIS_BATMON_LSB)		|
			(1 << GC_PMU_SETDIS_FST_BRNOUT_PWR_LSB)	|
			(1 << GC_PMU_SETDIS_FST_BRNOUT_LSB);

	/* Wait for powerdown */
	for (;;)
		__asm__("wfi");
}

/*
 * Exit hibernate mode
 * This function should be called after a powerdown exit event.
 * It handles turning the power domains back on.
 * Clocks will be left in RC no trim.
 */
void pmu_hibernate_exit(void)
{
	/* Turn on power to everything */
	GREG32(PMU, CLRDIS) = (1 << GC_PMU_CLRDIS_START_LSB)		|
				(1 << GC_PMU_CLRDIS_VDDL_LSB)		|
				(1 << GC_PMU_CLRDIS_VDDA_LSB)		|
				(1 << GC_PMU_CLRDIS_VDDSRM_LSB)		|
				(1 << GC_PMU_CLRDIS_VDDIOF_LSB)		|
				(1 << GC_PMU_CLRDIS_VDDLK_LSB)		|
				(1 << GC_PMU_CLRDIS_VDDSK_LSB)		|
				(1 << GC_PMU_CLRDIS_BIAS_LSB)		|
				(1 << GC_PMU_CLRDIS_BGAP_LSB)		|
				(1 << GC_PMU_CLRDIS_VDDXO_LSB)		|
				(1 << GC_PMU_CLRDIS_VDDXOLP_LSB)	|
				(1 << GC_PMU_CLRDIS_XTL_LSB)		|
				(1 << GC_PMU_CLRDIS_RC_TRIM_LSB)	|
				(1 << GC_PMU_CLRDIS_RC_NOTRIM_LSB)	|
				(1 << GC_PMU_CLRDIS_BATMON_LSB)		|
				(1 << GC_PMU_CLRDIS_FST_BRNOUT_PWR_LSB)	|
				(1 << GC_PMU_CLRDIS_FST_BRNOUT_LSB);
}

/*
 * Enter powerdown mode
 * This function does not return. The powerdown exit event will
 * cause the CPU to begin executing the system / app bootloader.
 * @warning The CPU must be in RC no trim mode
 */
void pmu_powerdown(void)
{
	/* Turn off power to everything */
	GREG32(PMU, SETDIS) = (1 << GC_PMU_SETDIS_START_LSB)		|
				(1 << GC_PMU_SETDIS_VDDL_LSB)		|
				(1 << GC_PMU_SETDIS_VDDA_LSB)		|
				(1 << GC_PMU_SETDIS_VDDSRM_LSB)		|
				(1 << GC_PMU_SETDIS_VDDIOF_LSB)		|
				(1 << GC_PMU_SETDIS_VDDLK_LSB)		|
				(1 << GC_PMU_SETDIS_VDDSK_LSB)		|
				(1 << GC_PMU_SETDIS_VDDSRK_LSB)		|
				(1 << GC_PMU_SETDIS_RETCOMPREF_LSB)	|
				(1 << GC_PMU_SETDIS_BIAS_LSB)		|
				(1 << GC_PMU_SETDIS_BGAP_LSB)		|
				(1 << GC_PMU_SETDIS_VDDXO_LSB)		|
				(1 << GC_PMU_SETDIS_VDDXOLP_LSB)	|
				(1 << GC_PMU_SETDIS_XTL_LSB)		|
				(1 << GC_PMU_SETDIS_RC_TRIM_LSB)	|
				(1 << GC_PMU_SETDIS_RC_NOTRIM_LSB)	|
				(1 << GC_PMU_SETDIS_BATMON_LSB)		|
				(1 << GC_PMU_SETDIS_FST_BRNOUT_PWR_LSB)	|
				(1 << GC_PMU_SETDIS_FST_BRNOUT_LSB);

	/* Wait for powerdown */
	for (;;)
		__asm__("wfi");
}

/*
 * Exit powerdown mode
 * This function should be called after a powerdown exit event.
 * It handles turning the power domains back on.
 * Clocks will be left in RC no trim.
 */
void pmu_powerdown_exit(void)
{
	/* Turn on power to everything */
	GREG32(PMU, CLRDIS) = (1 << GC_PMU_CLRDIS_START_LSB)	|
			(1 << GC_PMU_CLRDIS_VDDL_LSB)		|
			(1 << GC_PMU_CLRDIS_VDDA_LSB)		|
			(1 << GC_PMU_CLRDIS_VDDSRM_LSB)		|
			(1 << GC_PMU_CLRDIS_VDDIOF_LSB)		|
			(1 << GC_PMU_CLRDIS_VDDLK_LSB)		|
			(1 << GC_PMU_CLRDIS_VDDSK_LSB)		|
			(1 << GC_PMU_CLRDIS_VDDSRK_LSB)		|
			(1 << GC_PMU_CLRDIS_RETCOMPREF_LSB)	|
			(1 << GC_PMU_CLRDIS_BIAS_LSB)		|
			(1 << GC_PMU_CLRDIS_BGAP_LSB)		|
			(1 << GC_PMU_CLRDIS_VDDXO_LSB)		|
			(1 << GC_PMU_CLRDIS_VDDXOLP_LSB)	|
			(1 << GC_PMU_CLRDIS_XTL_LSB)		|
			(1 << GC_PMU_CLRDIS_RC_TRIM_LSB)	|
			(1 << GC_PMU_CLRDIS_RC_NOTRIM_LSB)	|
			(1 << GC_PMU_CLRDIS_BATMON_LSB)		|
			(1 << GC_PMU_CLRDIS_FST_BRNOUT_PWR_LSB)	|
			(1 << GC_PMU_CLRDIS_FST_BRNOUT_LSB);
}

/**
 * Handle PMU interrupt
 */
void pmu_interrupt(void)
{
	/* TBD */
}
DECLARE_IRQ(GC_IRQNUM_PMU_PMUINT, pmu_interrupt, 1);
