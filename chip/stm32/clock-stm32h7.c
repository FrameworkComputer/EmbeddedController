/* Copyright 2017 The Chromium OS Authors. All rights reserved.
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

/* High-speed oscillator default is 64 MHz */
#define STM32_HSI_CLOCK 64000000
/*
 * PLL1 configuration:
 * CPU freq = VCO / DIVP = HSI / DIVM * DIVN / DIVP
 *          = 64 / 4 * 50 / 2
 *          = 400 Mhz
 * System clock = 400 Mhz
 *  HPRE = /2  => AHB/Timer clock = 200 Mhz
 */
#if !defined(PLL1_DIVM) && !defined(PLL1_DIVN) && !defined(PLL1_DIVP)
#define PLL1_DIVM 4
#define PLL1_DIVN 50
#define PLL1_DIVP 2
#endif
#define PLL1_FREQ (STM32_HSI_CLOCK / PLL1_DIVM * PLL1_DIVN / PLL1_DIVP)

/* Flash latency settings for AHB/ACLK at 64 Mhz and Vcore in VOS1 range */
#define FLASH_ACLK_64MHZ (STM32_FLASH_ACR_WRHIGHFREQ_85MHZ | \
			  (0 << STM32_FLASH_ACR_LATENCY_SHIFT))
/* Flash latency settings for AHB/ACLK at 200 Mhz and Vcore in VOS1 range */
#define FLASH_ACLK_200MHZ (STM32_FLASH_ACR_WRHIGHFREQ_285MHZ | \
			   (2 << STM32_FLASH_ACR_LATENCY_SHIFT))

enum clock_osc {
	OSC_HSI = 0,	/* High-speed internal oscillator */
	OSC_CSI,	/* Multi-speed internal oscillator: NOT IMPLEMENTED */
	OSC_HSE,	/* High-speed external oscillator: NOT IMPLEMENTED */
	OSC_PLL,	/* PLL */
};

static int freq = STM32_HSI_CLOCK;
static int current_osc = OSC_HSI;

int clock_get_freq(void)
{
	return freq;
}

int clock_get_timer_freq(void)
{
	return clock_get_freq();
}

void clock_wait_bus_cycles(enum bus_type bus, uint32_t cycles)
{
	volatile uint32_t dummy __attribute__((unused));

	if (bus == BUS_AHB) {
		while (cycles--)
			dummy = STM32_GPIO_IDR(GPIO_A);
	} else { /* APB */
		while (cycles--)
			dummy = STM32_USART_BRR(STM32_USART1_BASE);
	}
}

static void clock_flash_latency(uint32_t target_acr)
{
	STM32_FLASH_ACR(0) = target_acr;
	while (STM32_FLASH_ACR(0) != target_acr)
		;
}

static void clock_enable_osc(enum clock_osc osc)
{
	uint32_t ready;
	uint32_t on;

	switch (osc) {
	case OSC_HSI:
		ready = STM32_RCC_CR_HSIRDY;
		on = STM32_RCC_CR_HSION;
		break;
	case OSC_PLL:
		ready = STM32_RCC_CR_PLL1RDY;
		on = STM32_RCC_CR_PLL1ON;
		break;
	default:
		return;
	}

	if (!(STM32_RCC_CR & ready)) {
		STM32_RCC_CR |= on;
		while (!(STM32_RCC_CR & ready))
			;
	}
}

static void clock_switch_osc(enum clock_osc osc)
{
	uint32_t sw;
	uint32_t sws;

	switch (osc) {
	case OSC_HSI:
		sw = STM32_RCC_CFGR_SW_HSI;
		sws = STM32_RCC_CFGR_SWS_HSI;
		break;
	case OSC_PLL:
		sw = STM32_RCC_CFGR_SW_PLL1;
		sws = STM32_RCC_CFGR_SWS_PLL1;
		break;
	default:
		return;
	}

	STM32_RCC_CFGR = sw;
	while ((STM32_RCC_CFGR & STM32_RCC_CFGR_SWS_MASK) != sws)
		;
}

static void clock_set_osc(enum clock_osc osc)
{
	if (osc == current_osc)
		return;

	hook_notify(HOOK_PRE_FREQ_CHANGE);

	switch (osc) {
	case OSC_HSI:
		/* Switch to HSI */
		clock_switch_osc(osc);
		freq = STM32_HSI_CLOCK;
		/* Restore /1 HPRE (AHB prescaler) */
		STM32_RCC_D1CFGR = STM32_RCC_D1CFGR_HPRE_DIV1
				 | STM32_RCC_D1CFGR_D1PPRE_DIV1
				 | STM32_RCC_D1CFGR_D1CPRE_DIV1;
		/* Use more optimized flash latency settings for 64-MHz ACLK */
		clock_flash_latency(FLASH_ACLK_64MHZ);
		/* Turn off the PLL1 to save power */
		STM32_RCC_CR &= ~STM32_RCC_CR_PLL1ON;
		break;

	case OSC_PLL:
		/* Configure PLL1 using 64 Mhz HSI as input */
		STM32_RCC_PLLCKSELR = STM32_RCC_PLLCKSEL_PLLSRC_HSI |
				      STM32_RCC_PLLCKSEL_DIVM1(PLL1_DIVM);
		/* in integer mode, wide range VCO with 16Mhz input, use divP */
		STM32_RCC_PLLCFGR = STM32_RCC_PLLCFG_PLL1VCOSEL_WIDE
				| STM32_RCC_PLLCFG_PLL1RGE_8M_16M
				| STM32_RCC_PLLCFG_DIVP1EN;
		STM32_RCC_PLL1DIVR = STM32_RCC_PLLDIV_DIVP(PLL1_DIVP)
				| STM32_RCC_PLLDIV_DIVN(PLL1_DIVN);
		/* turn on PLL1 and wait that it's ready */
		clock_enable_osc(OSC_PLL);
		/* Put /2 on HPRE (AHB prescaler) to keep at the 200Mhz max */
		STM32_RCC_D1CFGR = STM32_RCC_D1CFGR_HPRE_DIV2
				 | STM32_RCC_D1CFGR_D1PPRE_DIV1
				 | STM32_RCC_D1CFGR_D1CPRE_DIV1;
		freq = PLL1_FREQ / 2;
		/* Increase flash latency before transition the clock */
		clock_flash_latency(FLASH_ACLK_200MHZ);
		/* Switch to PLL */
		clock_switch_osc(OSC_PLL);
		break;
	default:
		break;
	}

	current_osc = osc;
	hook_notify(HOOK_FREQ_CHANGE);
}

void clock_enable_module(enum module_id module, int enable)
{
	/* Assume we have a single task using MODULE_FAST_CPU */
	if (module == MODULE_FAST_CPU)
		clock_set_osc(enable ? OSC_PLL : OSC_HSI);
}

void clock_init(void)
{
	/*
	 * STM32H743 Errata 2.2.15:
	 * 'Reading from AXI SRAM might lead to data read corruption'
	 *
	 * limit concurrent read access on AXI master to 1.
	 */
	STM32_AXI_TARG_FN_MOD(7) |= READ_ISS_OVERRIDE;

	/*
	 * Ensure the SPI is always clocked at the same frequency
	 * by putting it on the fixed 64-Mhz HSI clock.
	 * per_ck is clocked directly by the HSI (as per the default settings).
	 */
	STM32_RCC_D2CCIP1R = (STM32_RCC_D2CCIP1R &
		~(STM32_RCC_D2CCIP1R_SPI123SEL_MASK |
		  STM32_RCC_D2CCIP1R_SPI45SEL_MASK))
		| STM32_RCC_D2CCIP1R_SPI123SEL_PERCK
		| STM32_RCC_D2CCIP1R_SPI45SEL_HSI;

	/* Use more optimized flash latency settings for ACLK = HSI = 64 Mhz */
	clock_flash_latency(FLASH_ACLK_64MHZ);
}

static int command_clock(int argc, char **argv)
{
	if (argc >= 2) {
		if (!strcasecmp(argv[1], "hsi"))
			clock_set_osc(OSC_HSI);
		else if (!strcasecmp(argv[1], "pll"))
			clock_set_osc(OSC_PLL);
		else
			return EC_ERROR_PARAM1;
	}
	ccprintf("Clock frequency is now %d Hz\n", freq);
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(clock, command_clock,
			"hsi | pll", "Set clock frequency");
