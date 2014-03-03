/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Clocks and power management settings */

#include "chipset.h"
#include "clock.h"
#include "common.h"
#include "console.h"
#include "cpu.h"
#include "hooks.h"
#include "registers.h"
#include "util.h"

/* use 48Mhz USB-synchronized High-speed oscillator */
#define HSI48_CLOCK 48000000

int clock_get_freq(void)
{
	return HSI48_CLOCK;
}

void clock_enable_module(enum module_id module, int enable)
{
}

/*
 * system closk is HSI48 = 48MHz,
 * no prescaler, no MCO, no PLL
 * USB clock = HSI48
 */
BUILD_ASSERT(CPU_CLOCK == HSI48_CLOCK);

void clock_init(void)
{
	/*
	 * The initial state :
	 *  SYSCLK from HSI (=8MHz), no divider on AHB, APB1, APB2
	 *  PLL unlocked, RTC enabled on LSE
	 */

	/* put 1 Wait-State for flash access to ensure proper reads at 48Mhz */
	STM32_FLASH_ACR = 0x1001; /* 1 WS / Prefetch enabled */

	/* Ensure that HSI48 is ON */
	if (!(STM32_RCC_CR2 & (1 << 17))) {
		/* Enable HSI */
		STM32_RCC_CR2 |= 1 << 16;
		/* Wait for HSI to be ready */
		while (!(STM32_RCC_CR2 & (1 << 17)))
			;
	}
	/* switch SYSCLK to HSI48 */
	STM32_RCC_CFGR = 0x00000003;

	/* wait until the HSI48 is the clock source */
	while ((STM32_RCC_CFGR & 0xc) != 0xc)
		;
}
