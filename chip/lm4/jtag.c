/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "clock.h"
#include "jtag.h"
#include "registers.h"

void jtag_pre_init(void)
{
	/* Enable clocks to GPIO block C in run and sleep modes. */
	clock_enable_peripheral(CGC_OFFSET_GPIO, 0x0004,
			CGC_MODE_RUN | CGC_MODE_SLEEP);

	/*
	 * Ensure PC0:3 are set to JTAG function.  They should be set this way
	 * on a cold boot, but on a warm reboot a previous misbehaving image
	 * could have set them differently.
	 */
	if (((LM4_GPIO_PCTL(LM4_GPIO_C) & 0x0000ffff) == 0x00001111) &&
	    ((LM4_GPIO_AFSEL(LM4_GPIO_C) & 0x0f) == 0x0f) &&
	    ((LM4_GPIO_DEN(LM4_GPIO_C) & 0x0f) == 0x0f) &&
	    ((LM4_GPIO_PUR(LM4_GPIO_C) & 0x0f) == 0x0f))
		return;  /* Already properly configured */

	/* Unlock commit register for JTAG pins */
	LM4_GPIO_LOCK(LM4_GPIO_C) = LM4_GPIO_LOCK_UNLOCK;
	LM4_GPIO_CR(LM4_GPIO_C) |= 0x0f;

	/* Reset JTAG pins */
	LM4_GPIO_PCTL(LM4_GPIO_C) =
		(LM4_GPIO_PCTL(LM4_GPIO_C) & 0xffff0000) | 0x00001111;
	LM4_GPIO_AFSEL(LM4_GPIO_C) |= 0x0f;
	LM4_GPIO_DEN(LM4_GPIO_C) |= 0x0f;
	LM4_GPIO_PUR(LM4_GPIO_C) |= 0x0f;

	/* Re-lock commit register */
	LM4_GPIO_CR(LM4_GPIO_C) &= ~0x0f;
	LM4_GPIO_LOCK(LM4_GPIO_C) = 0;
}
