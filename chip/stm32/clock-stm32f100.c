/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Clocks and power management settings */

#include <stdint.h>

#include "board.h"
#include "clock.h"
#include "common.h"
#include "registers.h"
#include "util.h"

int clock_init(void)
{
	/*
	 * The initial state :
	 *  SYSCLK from HSI (=8MHz), no divider on AHB, APB1, APB2
	 *  PLL unlocked, RTC enabled on LSE
	 */

	/* Ensure that HSI is ON */
	if (!(STM32_RCC_CR & (1 << 1))) {
		/* Enable HSI */
		STM32_RCC_CR |= 1 << 0;
		/* Wait for HSI to be ready */
		while (!(STM32_RCC_CR & (1 << 1)))
			;
	}

	/*
	 * stays on HSI (8MHz), no prescaler, PLLSRC = HSI/2, PLLMUL = x4
	 * no MCO                      => PLLCLK = 16 Mhz
	 */
	BUILD_ASSERT(CPU_CLOCK == 16000000);
	STM32_RCC_CFGR = 0x00080000;
	/* Enable the PLL */
	STM32_RCC_CR |= 1 << 24;
	/* Wait for the PLL to lock */
	while (!(STM32_RCC_CR & (1 << 25)))
		;
	/* switch to SYSCLK to the PLL */
	STM32_RCC_CFGR = 0x00080002;
	/* wait until the PLL is the clock source */
	while ((STM32_RCC_CFGR & 0xc) != 0x8)
		;

	return EC_SUCCESS;
}
