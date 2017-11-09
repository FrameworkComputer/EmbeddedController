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
 * CPU freq = VCO / (DIVP + 1) = HSI / DIVM * DIVN / (DIVP + 1)
 *          = 64 * 4 * 50 / (4 + 1)
 *          = 160 Mhz
 */
#if !defined(PLL1_DIVM) && !defined(PLL1_DIVN) && !defined(PLL1_DIVP)
#define PLL1_DIVM 4
#define PLL1_DIVN 50
#define PLL1_DIVP 4
#endif
#define PLL1_FREQ (STM32_HSI_CLOCK / PLL1_DIVM * PLL1_DIVN / (PLL1_DIVP+1))

enum clock_osc {
	OSC_INIT = 0,	/* Uninitialized */
	OSC_HSI,	/* High-speed internal oscillator */
	OSC_CSI,	/* Multi-speed internal oscillator: NOT IMPLEMENTED */
	OSC_HSE,	/* High-speed external oscillator: NOT IMPLEMENTED */
	OSC_PLL,	/* PLL */
};

static int freq = STM32_HSI_CLOCK;
static int current_osc;

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
	uint32_t val;

	if (osc == current_osc)
		return;

	if (current_osc != OSC_INIT)
		hook_notify(HOOK_PRE_FREQ_CHANGE);

	switch (osc) {
	case OSC_HSI:
		/* Switch to HSI */
		clock_switch_osc(osc);

		freq = STM32_HSI_CLOCK;
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
		freq = PLL1_FREQ;

		/* Adjust flash latency */
		val = STM32_FLASH_ACR_WRHIGHFREQ_185MHZ |
				(2 << STM32_FLASH_ACR_LATENCY_SHIFT);
		STM32_FLASH_ACR = val;
		while (val != STM32_FLASH_ACR)
			;

		/* Switch to PLL */
		clock_switch_osc(OSC_PLL);
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

void clock_enable_module(enum module_id module, int enable)
{
	static uint32_t clock_mask;
	int new_mask;

	if (enable)
		new_mask = clock_mask | (1 << module);
	else
		new_mask = clock_mask & ~(1 << module);

	/* Only change clock if needed */
	if ((!!new_mask) != (!!clock_mask)) {

		/* Flush UART before switching clock speed */
		cflush();
#if 0 /* Power management policy: TODO(b/67081508) not implemented for now */
		clock_set_osc(new_mask ? OSC_PLL : OSC_CSI);
#endif
	}

	clock_mask = new_mask;
}

void clock_init(void)
{
#if 0 /* Keep default for now: HSI at 64 Mhz */
	clock_set_osc(OSC_PLL);
#endif
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
