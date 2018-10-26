/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "console.h"
#include "init_chip.h"
#include "registers.h"
#include "task.h"

#define CPRINTS(format, args...) cprints(CC_USB, format, ## args)

void init_jittery_clock_locking_optional(int highsec, int enable,
					 int lock_required)
{
	int rl = runlevel_is_high();

	if (lock_required) {
		CPRINTS("%s: run level %s, request to %sable",	__func__,
			rl ? "high" : "low", enable ? "en" : "dis");
	}

	if (rl) {
		uint32_t trimfast = GR_FUSE(RC_JTR_OSC60_CC_TRIM);
		uint32_t trim48 = GR_FUSE(RC_JTR_OSC48_CC_TRIM);
		uint32_t delta = (trim48 - trimfast);
		/*
		 * For metastability reasons, avoid clk_jtr ~= clk_timer, make
		 * a keepout region around 24MHz of about 0.75MHz, about 3/16
		 * of the the delta from trimfast and trim48
		 */
		uint32_t skiplow = (trim48 << 4) - (delta * 6);
		uint32_t skiphigh = (trim48 << 4) + (delta * 6);
		uint32_t setting = trimfast << 4;
		uint32_t stepx16;
		uint32_t bankval;
		int bank;

		if (highsec)
			stepx16 = (delta * 7) >> 1;
		else
			stepx16 = 2 * (trim48 - trimfast);

		for (bank = 0; bank < 16; bank++) {

			if (!enable) {
				/*
				 * Jitter should not be enabled, set all trims
				 * to the same value retrieved from the fuses.
				 * It is supposed to ensure that the internal
				 * clock runs at 48MHz.
				 */
				GR_XO_JTR_JITTERY_TRIM_BANK(bank) = trim48;
				continue;
			}
			/* saturate at 0xff */
			bankval = (setting > 0xfff) ? 0xff : (setting >> 4);

			GR_XO_JTR_JITTERY_TRIM_BANK(bank) = bankval;

			setting += stepx16;
			if ((setting > skiplow) && (setting < skiphigh))
				setting = skiphigh;
		}
	}

	GWRITE_FIELD(XO, CLK_JTR_TRIM_CTRL, RC_COARSE_TRIM_SRC, 2);
	GWRITE_FIELD(XO, CLK_JTR_TRIM_CTRL, RC_INITIAL_TRIM_PERIOD, 100);
	GWRITE_FIELD(XO, CLK_JTR_TRIM_CTRL, RC_TRIM_EN, 1);
	GREG32(XO, CLK_JTR_JITTERY_TRIM_EN) = 1;
	GREG32(XO, CLK_JTR_SYNC_CONTENTS) = 0;

	if (lock_required) {
		/* Writing any value locks things until the next hard reboot */
		GREG32(XO, CFG_WR_EN) = 0;
		GREG32(XO, JTR_CTRL_EN) = 0;
	}
}

void init_jittery_clock(int highsec)
{
	init_jittery_clock_locking_optional(highsec, 1, 1);
}

void init_sof_clock(void)
{
	/* Copy fuse value into software registers, both coarse and fine */
	unsigned coarseTrimVal = GR_FUSE(RC_TIMER_OSC48_CC_TRIM);
	unsigned fineTrimVal = GR_FUSE(RC_TIMER_OSC48_FC_TRIM);

	/* We think SOF toggle happens once every mS, or ~24000 clock ticks */
	unsigned targetCnt = PCLK_FREQ / 1000;

	/* The possible operations of a particular calibration bucket */
	unsigned binaryDnOp = 0x1 | 0x1 << 4;
	unsigned binaryUpOp = 0x1 | 0x0 << 4;
	unsigned subOp      = 0x3 | 0x1 << 4;
	unsigned addOp      = 0x2 | 0x1 << 4;
	unsigned nop        = 0;

	GREG32(XO, CLK_TIMER_RC_COARSE_ATE_TRIM) = coarseTrimVal;
	GREG32(XO, CLK_TIMER_RC_FINE_ATE_TRIM) = fineTrimVal;

	/* Coarse trim values come from software */
	GWRITE_FIELD(XO, CLK_TIMER_TRIM_CTRL, RC_COARSE_TRIM_SRC, 0);

	/* enable error interrupts
	 * This enables underrun and overflow interrupts */
	GREG32(XO, DXO_INT_ENABLE) = 0xC;

	/* Setup SOF calibration buckets and associated operations */
	GREG32(XO, CLK_TIMER_SLOW_CALIB0) = targetCnt * 70 / 100;
	GREG32(XO, CLK_TIMER_SLOW_CALIB1) = targetCnt * 80 / 100;
	GREG32(XO, CLK_TIMER_SLOW_CALIB2) = targetCnt * 90 / 100;
	GREG32(XO, CLK_TIMER_SLOW_CALIB3) =
		targetCnt * (1000000 - 1250) / 1000000;
	GREG32(XO, CLK_TIMER_SLOW_CALIB4) = targetCnt;
	GREG32(XO, CLK_TIMER_SLOW_CALIB5) =
		targetCnt * (1000000 + 1250) / 1000000;
	GREG32(XO, CLK_TIMER_SLOW_CALIB6) = targetCnt * 110 / 100;
	GREG32(XO, CLK_TIMER_SLOW_CALIB7) = targetCnt * 120 / 100;

	/* This is a work-around for the screwy SOF */
	GREG32(XO, CLK_TIMER_SLOW_CALIB_CTRL0) = nop;
	GREG32(XO, CLK_TIMER_SLOW_CALIB_CTRL1) = binaryDnOp;
	GREG32(XO, CLK_TIMER_SLOW_CALIB_CTRL2) = binaryDnOp;
	GREG32(XO, CLK_TIMER_SLOW_CALIB_CTRL3) = subOp;
	GREG32(XO, CLK_TIMER_SLOW_CALIB_CTRL4) = nop;
	GREG32(XO, CLK_TIMER_SLOW_CALIB_CTRL5) = nop;
	GREG32(XO, CLK_TIMER_SLOW_CALIB_CTRL6) = addOp;
	GREG32(XO, CLK_TIMER_SLOW_CALIB_CTRL7) = binaryUpOp;
	GREG32(XO, CLK_TIMER_SLOW_CALIB_CTRL8) = binaryUpOp;

	/* Set the calibration mode */
	GWRITE_FIELD(XO, CLK_TIMER_CALIB_TRIM_CTRL, ENABLE_FAST, 0);
	GWRITE_FIELD(XO, CLK_TIMER_CALIB_TRIM_CTRL, ENABLE_SLOW, 1);
	GWRITE_FIELD(XO, CLK_TIMER_CALIB_TRIM_CTRL, SLOW_MODE_SEL, 0); /* SOF */
	GWRITE_FIELD(XO, CLK_TIMER_CALIB_TRIM_CTRL, MAX_TRIM_SEL, 1);
	/* Don't stop when a NOP operation is seen, keep on calibrating */
	GWRITE_FIELD(XO, CLK_TIMER_CALIB_TRIM_CTRL, STOP_ON_NOP, 0);

	/* Set source of trim codes:
	 * coarse trim comes from software
	 * fine trim comes from calibration engine */
	GWRITE_FIELD(XO, CLK_TIMER_TRIM_CTRL, RC_COARSE_TRIM_SRC, 0);
	GWRITE_FIELD(XO, CLK_TIMER_TRIM_CTRL, RC_FINE_TRIM_SRC, 1);

	/* Enable dynamic trim */
	GWRITE_FIELD(XO, CLK_TIMER_TRIM_CTRL, RC_TRIM_EN, 1);

	/* Sync everything! */
	GREG32(XO, CLK_TIMER_SYNC_CONTENTS) = 1;

	/* Enable interrupts */
	task_enable_irq(GC_IRQNUM_XO0_SLOW_CALIB_UNDERRUN_INT);
	task_enable_irq(GC_IRQNUM_XO0_SLOW_CALIB_OVERFLOW_INT);
}

/* When the calibration under runs, it means the fine trim code
 * has reached 0, but the clock is still too slow.  Thus,
 * software must reduce the coarse trim code by 1 */
static void timer_sof_calibration_underrun_int(void)
{
	unsigned coarseTrimValue = GREG32(XO, CLK_TIMER_RC_COARSE_ATE_TRIM);

	if (coarseTrimValue > 0x00) {
		CPRINTS("%s: 0x%02x", __func__, coarseTrimValue);
		GREG32(XO, CLK_TIMER_RC_COARSE_ATE_TRIM) = coarseTrimValue - 1;
	}

	GREG32(XO, DXO_INT_STATE) =
		GC_XO_DXO_INT_STATE_SLOW_CALIB_UNDERRUN_MASK;
}
DECLARE_IRQ(GC_IRQNUM_XO0_SLOW_CALIB_UNDERRUN_INT,
	    timer_sof_calibration_underrun_int, 1);

/* When the calibration overflows, it means the fine trim code
 * has reached 0x1F, but the clock is still too fast.  Thus,
 * software must increase the coarse trim code by 1 */
static void timer_sof_calibration_overflow_int(void)
{
	unsigned coarseTrimValue = GREG32(XO, CLK_TIMER_RC_COARSE_ATE_TRIM);

	/* Coarse trim range is 0..0xff. */
	if (coarseTrimValue < 0xff) {
		CPRINTS("%s: 0x%02x", __func__, coarseTrimValue);
		GREG32(XO, CLK_TIMER_RC_COARSE_ATE_TRIM) = coarseTrimValue + 1;
	}

	GREG32(XO, DXO_INT_STATE) =
		GC_XO_DXO_INT_STATE_SLOW_CALIB_OVERFLOW_MASK;
}
DECLARE_IRQ(GC_IRQNUM_XO0_SLOW_CALIB_OVERFLOW_INT,
	    timer_sof_calibration_overflow_int, 1);

#ifdef DEBUG_ME
static int command_sof(int argc, char **argv)
{
	ccprintf("FUSE_RC_TIMER_OSC48_CC_TRIM) 0x%08x\n",
		 GR_FUSE(RC_TIMER_OSC48_CC_TRIM));
	ccprintf("FUSE_RC_TIMER_OSC48_FC_TRIM) 0x%08x\n",
		 GR_FUSE(RC_TIMER_OSC48_FC_TRIM));

	ccprintf("CLK_TIMER_RC_COARSE_ATE_TRIM 0x%08x\n",
		 GREG32(XO, CLK_TIMER_RC_COARSE_ATE_TRIM));
	ccprintf("CLK_TIMER_RC_FINE_ATE_TRIM   0x%08x\n",
		 GREG32(XO, CLK_TIMER_RC_FINE_ATE_TRIM));

	ccprintf("CLK_TIMER_TRIM_CTRL          0x%08x\n",
		 GREG32(XO, CLK_TIMER_TRIM_CTRL));

	ccprintf("CLK_TIMER_CALIB_TRIM_CTRL    0x%08x\n",
		 GREG32(XO, CLK_TIMER_CALIB_TRIM_CTRL));

	ccprintf("DXO_INT_ENABLE               0x%08x\n",
		 GREG32(XO, DXO_INT_ENABLE));

	ccprintf("CLK_TIMER_SLOW_CALIB\n");
	ccprintf("  0: 0x%04x\n", GREG32(XO, CLK_TIMER_SLOW_CALIB0));
	ccprintf("  1: 0x%04x\n", GREG32(XO, CLK_TIMER_SLOW_CALIB1));
	ccprintf("  2: 0x%04x\n", GREG32(XO, CLK_TIMER_SLOW_CALIB2));
	ccprintf("  3: 0x%04x\n", GREG32(XO, CLK_TIMER_SLOW_CALIB3));
	ccprintf("  4: 0x%04x\n", GREG32(XO, CLK_TIMER_SLOW_CALIB4));
	ccprintf("  5: 0x%04x\n", GREG32(XO, CLK_TIMER_SLOW_CALIB5));
	ccprintf("  6: 0x%04x\n", GREG32(XO, CLK_TIMER_SLOW_CALIB6));
	ccprintf("  7: 0x%04x\n", GREG32(XO, CLK_TIMER_SLOW_CALIB7));

	ccprintf("CLK_TIMER_SLOW_CALIB_CTRL\n");
	ccprintf("  0: 0x%02x\n", GREG32(XO, CLK_TIMER_SLOW_CALIB_CTRL0));
	ccprintf("  1: 0x%02x\n", GREG32(XO, CLK_TIMER_SLOW_CALIB_CTRL1));
	ccprintf("  2: 0x%02x\n", GREG32(XO, CLK_TIMER_SLOW_CALIB_CTRL2));
	ccprintf("  3: 0x%02x\n", GREG32(XO, CLK_TIMER_SLOW_CALIB_CTRL3));
	ccprintf("  4: 0x%02x\n", GREG32(XO, CLK_TIMER_SLOW_CALIB_CTRL4));
	ccprintf("  5: 0x%02x\n", GREG32(XO, CLK_TIMER_SLOW_CALIB_CTRL5));
	ccprintf("  6: 0x%02x\n", GREG32(XO, CLK_TIMER_SLOW_CALIB_CTRL6));
	ccprintf("  7: 0x%02x\n", GREG32(XO, CLK_TIMER_SLOW_CALIB_CTRL7));
	ccprintf("  8: 0x%02x\n", GREG32(XO, CLK_TIMER_SLOW_CALIB_CTRL8));

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(sof, command_sof,
			"",
			"Display the SoF clock stuff");
#endif	/* DEBUG_ME */
