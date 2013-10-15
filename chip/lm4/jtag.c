/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
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
	/* Enable clocks to GPIO block C in run and sleep modes. */
	clock_enable_peripheral(CGC_OFFSET_GPIO, 0x0004, CGC_MODE_ALL);

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

	/* Set interrupt on either edge of the JTAG signals */
	LM4_GPIO_IS(LM4_GPIO_C) &= ~0x0f;
	LM4_GPIO_IBE(LM4_GPIO_C) |= 0x0f;

	/* Re-lock commit register */
	LM4_GPIO_CR(LM4_GPIO_C) &= ~0x0f;
	LM4_GPIO_LOCK(LM4_GPIO_C) = 0;
}

#ifdef CONFIG_LOW_POWER_IDLE
void jtag_interrupt(enum gpio_signal signal)
{
	/*
	 * This interrupt is the first sign someone is trying to use
	 * the JTAG. Disable slow speed sleep so that the JTAG action
	 * can take place.
	 */
	disable_sleep(SLEEP_MASK_JTAG);

	/*
	 * Once we get this interrupt, disable it from occurring again
	 * to avoid repeated interrupts when debugging via JTAG.
	 */
	gpio_disable_interrupt(GPIO_JTAG_TCK);
}
#endif /* CONFIG_LOW_POWER_IDLE */

