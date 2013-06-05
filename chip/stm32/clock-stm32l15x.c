/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Clocks and power management settings */

#include "clock.h"
#include "common.h"
#include "registers.h"
#include "util.h"

BUILD_ASSERT(CPU_CLOCK == 16000000);

void enable_sleep(uint32_t mask)
{
	/* low power mode not implemented */
}

void disable_sleep(uint32_t mask)
{
	/* low power mode not implemented */
}

void clock_init(void)
{
	uint32_t tmp_acr;

	/*
	 * The initial state :
	 *  SYSCLK from MSI (=2MHz), no divider on AHB, APB1, APB2
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
	 * Set the recommended flash settings for 16MHz clock.
	 *
	 * The 3 bits must be programmed strictly sequentially,
	 * but it is faster not to read-back the value of the ACR register
	 * in the middle of the sequence so let's use a temporary variable.
	 */
	tmp_acr = STM32_FLASH_ACR;
	/* Enable 64-bit access */
	tmp_acr |= (1 << 2);
	STM32_FLASH_ACR = tmp_acr;
	/* Enable Prefetch Buffer */
	tmp_acr |= (1 << 1);
	STM32_FLASH_ACR = tmp_acr;
	/* Flash 1 wait state */
	tmp_acr |= (1 << 0);
	STM32_FLASH_ACR = tmp_acr;

#ifdef CONFIG_USE_PLL
	/*
	 * Switch to HSI, no prescaler, PLLSRC = HSI, PLLMUL = x3, PLLDIV = /3,
	 * no MCO => PLLVCO = 48 MHz and PLLCLK = 16 Mhz.
	 */
	STM32_RCC_CFGR = 0x00800001;

	/* Enable the PLL */
	STM32_RCC_CR |= 1 << 24;
	/* Wait for the PLL to lock */
	while (!(STM32_RCC_CR & (1 << 25)))
		;
	/* switch to SYSCLK to the PLL */
	STM32_RCC_CFGR = 0x00800003;
	/* wait until the PLL is the clock source */
	while ((STM32_RCC_CFGR & 0xc) != 0xc)
		;
#else
	/* Switch to HSI */
	STM32_RCC_CFGR = 0x00000001;
#endif
}
