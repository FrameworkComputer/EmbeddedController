/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "clock.h"
#include "gpio.h"
#include "jtag.h"
#include "registers.h"
#include "system.h"

void jtag_pre_init(void)
{
	/* Setting for fixing JTAG issue */
	NPCX_DBGCTRL = 0x04;
	/* Enable automatic freeze mode */
	CLEAR_BIT(NPCX_DBGFRZEN3, NPCX_DBGFRZEN3_GLBL_FRZ_DIS);

	/*
	 * Enable JTAG functionality by SW without pulling down strap-pin
	 * nJEN0 or nJEN1 during ec POWERON or VCCRST reset occurs.
	 * Please notice it will change pinmux to JTAG directly.
	 */
#ifdef NPCX_ENABLE_JTAG
#if NPCX_JTAG_MODULE2
	CLEAR_BIT(NPCX_DEVALT(ALT_GROUP_5), NPCX_DEVALT5_NJEN1_EN);
#else
	CLEAR_BIT(NPCX_DEVALT(ALT_GROUP_5), NPCX_DEVALT5_NJEN0_EN);
#endif
#endif
}
