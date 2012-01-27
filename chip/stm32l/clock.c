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

/**
 * Idle task
 * executed when no task are ready to be scheduled
 */
void __idle(void)
{
	while (1) {
		/* wait for the irq event */
		asm("wfi");
		/* TODO more power management here */
	}
}

int clock_init(void)
{
	/*
	 * The initial state :
	 *  SYSCLK from HSI (=16MHz), no divider on AHB, APB1, APB2
	 *  PLL unlocked, RTC enabled on LSE
	 */

	/* Ensure that HSI is ON */
	if (!(STM32L_RCC_CR & (1 << 1))) {
		/* Enable HSI */
		STM32L_RCC_CR |= 1 << 0;
		/* Wait for HSI to be ready */
		while (!(STM32L_RCC_CR & (1 << 1)))
			;
	}

	/*
	 * stays on HSI, no prescaler, PLLSRC = HSI, PLLMUL = x3, PLLDIV = /3,
	 * no MCO                      => PLLVCO = 48 MHz and PLLCLK = 16 Mhz
	 */
	BUILD_ASSERT(CPU_CLOCK == 16000000);
	STM32L_RCC_CFGR = 0x00800001;
	/* Enable the PLL */
	STM32L_RCC_CR |= 1 << 24;
	/* Wait for the PLL to lock */
	while (!(STM32L_RCC_CR & (1 << 25)))
		;
	/* switch to SYSCLK to the PLL */
	STM32L_RCC_CFGR = 0x00800003;

	/* switch on LSI */
	STM32L_RCC_CSR |= 1 << 0;
	/* Wait for LSI to be ready */
	while (!(STM32L_RCC_CSR & (1 << 1)))
		;
	/* Enable RTC and use LSI as clock source */
	STM32L_RCC_CSR = (STM32L_RCC_CSR & ~0x00430000) | 0x00420000;

	return EC_SUCCESS;
}
