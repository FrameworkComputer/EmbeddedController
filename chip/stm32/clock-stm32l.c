/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Clocks and power management settings */

#include "clock.h"
#include "common.h"
#include "console.h"
#include "cpu.h"
#include "hooks.h"
#include "registers.h"
#include "util.h"

/* High-speed oscillator is 16 MHz */
#define HSI_CLOCK 16000000
/*
 * MSI is 2 MHz (default) 1 MHz, depending on ICSCR setting.  We use 1 MHz
 * because it's the lowest clock rate we can still run 115200 baud serial
 * for the debug console.
 */
#define MSI_2MHZ_CLOCK (1 << 21)
#define MSI_1MHZ_CLOCK (1 << 20)

enum clock_osc {
	OSC_INIT = 0,	/* Uninitialized */
	OSC_HSI,	/* High-speed oscillator */
	OSC_MSI,	/* Med-speed oscillator @ 1 MHz */
};

static int freq;
static int current_osc;

void enable_sleep(uint32_t mask)
{
	/* low power mode not implemented */
}

void disable_sleep(uint32_t mask)
{
	/* low power mode not implemented */
}

int clock_get_freq(void)
{
	return freq;
}

/**
 * Set which oscillator is used for the clock
 *
 * @param osc		Oscillator to use
 */
static void clock_set_osc(enum clock_osc osc)
{
	uint32_t tmp_acr;

	if (osc == current_osc)
		return;

	switch (osc) {
	case OSC_HSI:
		/* Ensure that HSI is ON */
		if (!(STM32_RCC_CR & STM32_RCC_CR_HSIRDY)) {
			/* Enable HSI */
			STM32_RCC_CR |= STM32_RCC_CR_HSION;
			/* Wait for HSI to be ready */
			while (!(STM32_RCC_CR & STM32_RCC_CR_HSIRDY))
				;
		}

		/* Disable LPSDSR */
		STM32_PWR_CR &= ~STM32_PWR_CR_LPSDSR;

		/*
		 * Set the recommended flash settings for 16MHz clock.
		 *
		 * The 3 bits must be programmed strictly sequentially, but it
		 * is faster not to read-back the value of the ACR register in
		 * the middle of the sequence so use a temporary variable.
		 */
		tmp_acr = STM32_FLASH_ACR;
		/* Enable 64-bit access */
		tmp_acr |= STM32_FLASH_ACR_ACC64;
		STM32_FLASH_ACR = tmp_acr;
		/* Enable Prefetch Buffer */
		tmp_acr |= STM32_FLASH_ACR_PRFTEN;
		STM32_FLASH_ACR = tmp_acr;
		/* Flash 1 wait state */
		tmp_acr |= STM32_FLASH_ACR_LATENCY;
		STM32_FLASH_ACR = tmp_acr;
		/* Switch to HSI */
		STM32_RCC_CFGR = STM32_RCC_CFGR_SW_HSI;

		freq = HSI_CLOCK;
		break;

	case OSC_MSI:
		/* Switch to MSI @ 1MHz */
		STM32_RCC_ICSCR =
			(STM32_RCC_ICSCR & ~STM32_RCC_ICSCR_MSIRANGE_MASK) |
			STM32_RCC_ICSCR_MSIRANGE_1MHZ;
		STM32_RCC_CFGR = STM32_RCC_CFGR_SW_MSI;

		/*
		 * Set the recommended flash settings for <= 2MHz clock.
		 *
		 * The 3 bits must be programmed strictly sequentially, but it
		 * is faster not to read-back the value of the ACR register in
		 * the middle of the sequence so use a temporary variable.
		 */
		tmp_acr = STM32_FLASH_ACR;
		/* Flash 0 wait state */
		tmp_acr &= ~STM32_FLASH_ACR_LATENCY;
		STM32_FLASH_ACR = tmp_acr;
		/* Disable prefetch Buffer */
		tmp_acr &= ~STM32_FLASH_ACR_PRFTEN;
		STM32_FLASH_ACR = tmp_acr;
		/* Disable 64-bit access */
		tmp_acr &= ~STM32_FLASH_ACR_ACC64;
		STM32_FLASH_ACR = tmp_acr;

		/* Disable HSI */
		STM32_RCC_CR &= STM32_RCC_CR_HSION;

		/* Enable LPSDSR */
		STM32_PWR_CR |= STM32_PWR_CR_LPSDSR;

		freq = MSI_1MHZ_CLOCK;
		break;

	default:
		break;
	}

	/* Notify modules of frequency change unless we're initializing */
	if (current_osc != OSC_INIT) {
		current_osc = osc;
		hook_notify(HOOK_FREQ_CHANGE);
	} else {
		current_osc = osc;
	}
}

void clock_init(void)
{
	/*
	 * The initial state :
	 *  SYSCLK from MSI (=2MHz), no divider on AHB, APB1, APB2
	 *  PLL unlocked, RTC enabled on LSE
	 */

	/* Switch to high-speed oscillator */
	clock_set_osc(1);
}

static void clock_chipset_startup(void)
{
	/* Return to full speed */
	clock_set_osc(OSC_HSI);
}
DECLARE_HOOK(HOOK_CHIPSET_STARTUP, clock_chipset_startup, HOOK_PRIO_DEFAULT);
DECLARE_HOOK(HOOK_CHIPSET_RESUME, clock_chipset_startup, HOOK_PRIO_DEFAULT);

static void clock_chipset_shutdown(void)
{
	/* Drop to lower clock speed */
	clock_set_osc(OSC_MSI);
}
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, clock_chipset_shutdown, HOOK_PRIO_DEFAULT);
DECLARE_HOOK(HOOK_CHIPSET_SUSPEND, clock_chipset_shutdown, HOOK_PRIO_DEFAULT);

static int command_clock(int argc, char **argv)
{
	if (argc >= 2) {
		if (!strcasecmp(argv[1], "hsi"))
			clock_set_osc(OSC_HSI);
		else if (!strcasecmp(argv[1], "msi"))
			clock_set_osc(OSC_MSI);
		else
			return EC_ERROR_PARAM1;
	}

	ccprintf("Clock frequency is now %d Hz\n", freq);
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(clock, command_clock,
			"hsi | msi",
			"Set clock frequency",
			NULL);
