/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "console.h"
#include "init_chip.h"
#include "registers.h"

void init_jittery_clock(int highsec)
{
	unsigned trimfast = GR_FUSE(RC_JTR_OSC60_CC_TRIM);
	unsigned trim48 = GR_FUSE(RC_JTR_OSC48_CC_TRIM);
	unsigned delta = (trim48 - trimfast);
	/* for metastability reasons, avoid clk_jtr ~= clk_timer, make
	 * a keepout region around 24MHz of about 0.75MHz, about 3/16 of the
	 * the delta from trimfast and trim48 */
	unsigned skiplow = (trim48 << 4) - (delta * 6);
	unsigned skiphigh = (trim48 << 4) + (delta * 6);
	unsigned setting = trimfast << 4;
	unsigned stepx16;
	unsigned bankval;
	int bank;

	if (highsec)
		stepx16 = 0xff - trimfast;
	else
		stepx16 = 2 * (trim48 - trimfast);

	for (bank = 0; bank < 16; bank++) {
		/* saturate at 0xff */
		bankval = (setting > 0xfff) ? 0xff : (setting >> 4);

		GR_XO_JTR_JITTERY_TRIM_BANK(bank) = bankval;

		setting += stepx16;
		if ((setting > skiplow) && (setting < skiphigh))
			setting = skiphigh;
	}

	GWRITE_FIELD(XO, CLK_JTR_TRIM_CTRL, RC_COARSE_TRIM_SRC, 2);
	GWRITE_FIELD(XO, CLK_JTR_TRIM_CTRL, RC_INITIAL_TRIM_PERIOD, 100);
	GWRITE_FIELD(XO, CLK_JTR_TRIM_CTRL, RC_TRIM_EN, 1);
	GREG32(XO, CLK_JTR_JITTERY_TRIM_EN) = 1;
	GREG32(XO, CLK_JTR_SYNC_CONTENTS) = 0;

	/* Writing any value locks things until the next hard reboot */
	GREG32(XO, CFG_WR_EN) = 0;
	GREG32(XO, JTR_CTRL_EN) = 0;
}
