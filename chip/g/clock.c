/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "clock.h"
#include "registers.h"

/* Clock initialization taken from some example code */

#define RESOLUTION 12
#define LOAD_VAL (0x1 << 11)
#define MAX_TRIM (7*16)

static void switch_osc_to_xtl(void);

static void clock_on_xo0(void)
{
	/* turn on xo0 clock */
	/* don't know which control word it might be in */
#ifdef G_PMU_PERICLKSET0_DXO0_LSB
	G_PMU_PERICLKSET0 = (1 << G_PMU_PERICLKSET0_DXO0_LSB);
#endif

#ifdef G_PMU_PERICLKSET1_DXO0_LSB
	G_PMU_PERICLKSET1 = (1 << G_PMU_PERICLKSET1_DXO0_LSB);
#endif
}

/* Converts an integer setting to the RC trim code format (7, 4-bit values) */
static unsigned val_to_trim_code(unsigned val)
{
	unsigned base = val / 7;
	unsigned mod = val % 7;
	unsigned code = 0x0;
	int digit;

	/* Increasing count from right to left */
	for (digit = 0; digit < 7; digit++) {
		/* Check for mod */
		if (digit <= mod)
			code |= ((base & 0xF) << 4 * digit);
		else
			code |= (((base - 1) & 0xF) << 4 * digit);
	}

	return code;
}

static unsigned calib_rc_trim(void)
{
	unsigned size, iter;
	signed mid;
	signed diff;

	/* Switch to crystal for calibration. This should work since we are
	 * technically on an uncalibrated RC trim clock. */
	switch_osc_to_xtl();

	clock_on_xo0();

	/* Clear the HOLD signal on dxo */
	G_XO_OSC_CLRHOLD = G_XO_OSC_CLRHOLD_RC_TRIM_MASK;

	/* Reset RC calibration counters */
	G_XO_OSC_RC_CAL_RSTB = 0x0;
	G_XO_OSC_RC_CAL_RSTB = 0x1;

	/* Write the LOAD val */
	G_XO_OSC_RC_CAL_LOAD = LOAD_VAL;

	/* Begin binary search */
	mid = 0;
	size = MAX_TRIM / 2;
	for (iter = 0; iter <= 7; iter++) {
		/* Set the trim value */
		G_XO_OSC_RC = val_to_trim_code(mid) << G_XO_OSC_RC_TRIM_LSB;

		/* Do a calibration */
		G_XO_OSC_RC_CAL_START = 0x1;

		/* NOTE: There is a small race condition because of the delay
		 * in dregfile. The start doesn't actually appear for 2 clock
		 * cycles after the write. So, poll until done goes low. */
		while (G_XO_OSC_RC_CAL_DONE)
			;

		/* Wait until it's done */
		while (!G_XO_OSC_RC_CAL_DONE)
			;

		/* Check the counter value */
		diff = LOAD_VAL - G_XO_OSC_RC_CAL_COUNT;

		/* Test to see whether we are still outside of our desired
		 * resolution */
		if ((diff < -RESOLUTION) || (diff > RESOLUTION))
			mid = (diff > 0) ? (mid - size / 2) : (mid + size / 2);

		size = (size + 1) >> 1;	/* round up before division */
	}

	/* Set the final trim value */
	G_XO_OSC_RC = (val_to_trim_code(mid) << G_XO_OSC_RC_TRIM_LSB) |
	    (0x1 << G_XO_OSC_RC_EN_LSB);

	/* Set the HOLD signal on dxo */
	G_XO_OSC_SETHOLD = G_XO_OSC_SETHOLD_RC_TRIM_MASK;

	/* Switch back to the RC trim now that we are calibrated */
	G_PMU_OSC_HOLD_CLR = 0x1;	/* make sure the hold signal is clear */
	G_PMU_OSC_SELECT = G_PMU_OSC_SELECT_RC_TRIM;
	/* Make sure the hold signal is set for future power downs */
	G_PMU_OSC_HOLD_SET = 0x1;

	return mid;
}

static void switch_osc_to_rc_trim(void)
{
	unsigned trimmed;
	unsigned saved_trim, fuse_trim, default_trim;
	unsigned trim_code;

	/* check which clock we are running on */
	unsigned osc_sel = G_PMU_OSC_SELECT_STAT;

	if (osc_sel == G_PMU_OSC_SELECT_RC_TRIM) {
		/* already using the rc_trim so nothing to do here */
		/* make sure the hold signal is set for future power downs */
		G_PMU_OSC_HOLD_SET = 0x1;
		return;
	}

	/* Turn on DXO clock so we can write in the trim code in */
	clock_on_xo0();

	/* disable the RC_TRIM Clock */
	REG_WRITE_MASK(G_PMU_OSC_CTRL,
		       G_PMU_OSC_CTRL_RC_TRIM_READYB_MASK, 0x1,
		       G_PMU_OSC_CTRL_RC_TRIM_READYB_LSB);

	/* power up the clock if not already powered up */
	G_PMU_CLRDIS = 1 << G_PMU_SETDIS_RC_TRIM_LSB;

	/* Try to find the trim code */
	saved_trim = G_XO_OSC_RC_STATUS;
	fuse_trim = G_PMU_FUSE_RD_RC_OSC_26MHZ;
	default_trim = G_XO_OSC_RC;

	/* Check for the trim code in the always-on domain before looking at
	 * the fuse */
	if (saved_trim & G_XO_OSC_RC_STATUS_EN_MASK) {
		trim_code = (saved_trim & G_XO_OSC_RC_STATUS_TRIM_MASK)
		    >> G_XO_OSC_RC_STATUS_TRIM_LSB;
		trimmed = 1;
	} else if (fuse_trim & G_PMU_FUSE_RD_RC_OSC_26MHZ_EN_MASK) {
		trim_code = (fuse_trim & G_PMU_FUSE_RD_RC_OSC_26MHZ_TRIM_MASK)
		    >> G_PMU_FUSE_RD_RC_OSC_26MHZ_TRIM_LSB;
		trimmed = 1;
	} else {
		trim_code = (default_trim & G_XO_OSC_RC_TRIM_MASK)
		    >> G_XO_OSC_RC_TRIM_LSB;
		trimmed = 0;
	}

	/* Write the trim code to dxo */
	if (trimmed) {
		/* clear the hold signal */
		G_XO_OSC_CLRHOLD = 1 << G_XO_OSC_CLRHOLD_RC_TRIM_LSB;
		G_XO_OSC_RC = (trim_code << G_XO_OSC_RC_TRIM_LSB) | /* write */
		    ((trimmed & 0x1) << G_XO_OSC_RC_EN_LSB); /* enable */
		/* set the hold signal */
		G_XO_OSC_SETHOLD = 1 << G_XO_OSC_SETHOLD_RC_TRIM_LSB;
	}

	/* enable the RC_TRIM Clock */
	REG_WRITE_MASK(G_PMU_OSC_CTRL, G_PMU_OSC_CTRL_RC_TRIM_READYB_MASK,
		       0x0, G_PMU_OSC_CTRL_RC_TRIM_READYB_LSB);

	/* Switch the select signal */
	G_PMU_OSC_HOLD_CLR = 0x1;	/* make sure the hold signal is clear */
	G_PMU_OSC_SELECT = G_PMU_OSC_SELECT_RC_TRIM;
	/* make sure the hold signal is set for future power downs */
	G_PMU_OSC_HOLD_SET = 0x1;

	/* If we didn't find a valid trim code, then we need to calibrate */
	if (!trimmed)
		calib_rc_trim();
	/* We saved the trim code and went back to the RC trim inside
	 * calib_rc_trim */
}

static void switch_osc_to_xtl(void)
{
	unsigned int saved_trim, fuse_trim, trim_code, final_trim;
	unsigned int fsm_status, max_trim;
	unsigned int fsm_done;
	/* check which clock we are running on */
	unsigned int osc_sel = G_PMU_OSC_SELECT_STAT;

	if (osc_sel == G_PMU_OSC_SELECT_XTL) {
		/* already using the crystal so nothing to do here */
		/* make sure the hold signal is set for future power downs */
		G_PMU_OSC_HOLD_SET = 0x1;
		return;
	}

	if (osc_sel == G_PMU_OSC_SELECT_RC)
		/* RC untrimmed clock. We must go through the trimmed clock
		 * first to avoid glitching */
		switch_osc_to_rc_trim();

	/* disable the XTL Clock */
	REG_WRITE_MASK(G_PMU_OSC_CTRL, G_PMU_OSC_CTRL_XTL_READYB_MASK,
		       0x1, G_PMU_OSC_CTRL_XTL_READYB_LSB);

	/* power up the clock if not already powered up */
	G_PMU_CLRDIS = 1 << G_PMU_SETDIS_XTL_LSB;

	/* Try to find the trim code */
	trim_code = 0;
	saved_trim = G_XO_OSC_XTL_TRIM_STAT;
	fuse_trim = G_PMU_FUSE_RD_XTL_OSC_26MHZ;

	/* Check for the trim code in the always-on domain before looking at
	 * the fuse */
	if (saved_trim & G_XO_OSC_XTL_TRIM_STAT_EN_MASK) {
		/* nothing to do */
		/* trim_code = (saved_trim & G_XO_OSC_XTL_TRIM_STAT_CODE_MASK)
		   >> G_XO_OSC_XTL_TRIM_STAT_CODE_LSB; */
		/* print_trickbox_message("XTL TRIM CODE FOUND IN 3P3"); */
	} else if (fuse_trim & G_PMU_FUSE_RD_XTL_OSC_26MHZ_EN_MASK) {
		/* push the fuse trim code as the saved trim code */
		/* print_trickbox_message("XTL TRIM CODE FOUND IN FUSE"); */
		trim_code = (fuse_trim & G_PMU_FUSE_RD_XTL_OSC_26MHZ_TRIM_MASK)
		    >> G_PMU_FUSE_RD_XTL_OSC_26MHZ_TRIM_LSB;
		/* make sure the hold signal is clear */
		G_XO_OSC_CLRHOLD = 0x1 << G_XO_OSC_CLRHOLD_XTL_LSB;
		G_XO_OSC_XTL_TRIM =
		    (trim_code << G_XO_OSC_XTL_TRIM_CODE_LSB) |
		    (0x1 << G_XO_OSC_XTL_TRIM_EN_LSB);
	} else
		/* print_trickbox_message("XTL TRIM CODE NOT FOUND"); */
		;

	/* Run the crystal FSM to calibrate the crystal trim */
	fsm_done = G_XO_OSC_XTL_FSM;
	if (fsm_done & G_XO_OSC_XTL_FSM_DONE_MASK) {
		/* If FSM done is high, it means we already ran it so let's not
		 * run it again */
		/* DO NOTHING */
	} else {
		G_XO_OSC_XTL_FSM_EN = 0x0;	/* reset FSM */
		G_XO_OSC_XTL_FSM_EN = G_XO_OSC_XTL_FSM_EN_KEY;
		while (!(fsm_done & G_XO_OSC_XTL_FSM_DONE_MASK))
			fsm_done = G_XO_OSC_XTL_FSM;
	}

	/* Check the status and final trim value */
	max_trim = (G_XO_OSC_XTL_FSM_CFG & G_XO_OSC_XTL_FSM_CFG_TRIM_MAX_MASK)
	    >> G_XO_OSC_XTL_FSM_CFG_TRIM_MAX_LSB;
	final_trim = (fsm_done & G_XO_OSC_XTL_FSM_TRIM_MASK)
	    >> G_XO_OSC_XTL_FSM_TRIM_LSB;
	fsm_status = (fsm_done & G_XO_OSC_XTL_FSM_STATUS_MASK)
	    >> G_XO_OSC_XTL_FSM_STATUS_LSB;

	/* Check status bit and trim value */
	if (fsm_status) {
		if (final_trim >= max_trim)
			/* print_trickbox_error("ERROR: XTL FSM status was
			   high, but final XTL trim is greater than or equal to
			   max trim"); */
			;
	} else {
		if (final_trim != max_trim)
			/* print_trickbox_error("ERROR: XTL FSM status was low,
			   but final XTL trim does not equal max trim"); */
			;
	}

	/* save the trim for future powerups */
	/* make sure the hold signal is clear (may have already been cleared) */
	G_XO_OSC_CLRHOLD = 0x1 << G_XO_OSC_CLRHOLD_XTL_LSB;
	G_XO_OSC_XTL_TRIM =
	    (final_trim << G_XO_OSC_XTL_TRIM_CODE_LSB) |
	    (0x1 << G_XO_OSC_XTL_TRIM_EN_LSB);
	/* make sure the hold signal is set for future power downs */
	G_XO_OSC_SETHOLD = 0x1 << G_XO_OSC_SETHOLD_XTL_LSB;

	/* enable the XTL Clock */
	REG_WRITE_MASK(G_PMU_OSC_CTRL, G_PMU_OSC_CTRL_XTL_READYB_MASK,
		       0x0, G_PMU_OSC_CTRL_XTL_READYB_LSB);

	/* Switch the select signal */
	G_PMU_OSC_HOLD_CLR = 0x1;	/* make sure the hold signal is clear */
	G_PMU_OSC_SELECT = G_PMU_OSC_SELECT_XTL;
	/* make sure the hold signal is set for future power downs */
	G_PMU_OSC_HOLD_SET = 0x1;
}

void clock_init(void)
{
	/*
	 * TODO(crosbug.com/p/33813): The following comment was in the example
	 * code, but the function that's called doesn't match what it says.
	 * Investigate further.
	 */

	/* Switch to crystal clock since RC clock not accurate enough */
	switch_osc_to_rc_trim();
}

void clock_enable_module(enum module_id module, int enable)
{
	if (module != MODULE_UART)
		return;

	/* don't know which control word it might be in */
#ifdef G_PMU_PERICLKSET0_DUART0_LSB
	REG_WRITE_MASK(G_PMU_PERICLKSET0,
		       1 << G_PMU_PERICLKSET0_DUART0_LSB, enable,
		       G_PMU_PERICLKSET0_DUART0_LSB);
#endif

#ifdef G_PMU_PERICLKSET1_DUART0_LSB
	REG_WRITE_MASK(G_PMU_PERICLKSET1,
		       1 << G_PMU_PERICLKSET1_DUART0_LSB, enable,
		       G_PMU_PERICLKSET0_DUART1_LSB);
#endif
}
