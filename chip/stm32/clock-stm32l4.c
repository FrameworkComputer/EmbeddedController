/* Copyright 2016 The Chromium OS Authors. All rights reserved.
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

/* High-speed oscillator is 16 MHz */
#define STM32_HSI_CLOCK 16000000
/* Multi-speed oscillator is 4 MHz by default */
#define STM32_MSI_CLOCK 4000000

enum clock_osc {
	OSC_INIT = 0,	/* Uninitialized */
	OSC_HSI,	/* High-speed internal oscillator */
	OSC_MSI,	/* Multi-speed internal oscillator */
#ifdef STM32_HSE_CLOCK	/* Allows us to catch absence of HSE at comiple time */
	OSC_HSE,	/* High-speed external oscillator */
#endif
	OSC_PLL,	/* PLL */
};

static int freq = STM32_MSI_CLOCK;
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
	volatile uint32_t unused __attribute__((unused));

	if (bus == BUS_AHB) {
		while (cycles--)
			unused = STM32_DMA1_REGS->isr;
	} else { /* APB */
		while (cycles--)
			unused = STM32_USART_BRR(STM32_USART1_BASE);
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
	case OSC_MSI:
		ready = STM32_RCC_CR_MSIRDY;
		on = STM32_RCC_CR_MSION;
		break;
#ifdef STM32_HSE_CLOCK
	case OSC_HSE:
		ready = STM32_RCC_CR_HSERDY;
		on = STM32_RCC_CR_HSEON;
		break;
#endif
	case OSC_PLL:
		ready = STM32_RCC_CR_PLLRDY;
		on = STM32_RCC_CR_PLLON;
		break;
	default:
		return;
	}

	/* Enable HSI and wait for HSI to be ready */
	wait_for_ready(&STM32_RCC_CR, on, ready);
}

/* Switch system clock oscillator */
static void clock_switch_osc(enum clock_osc osc)
{
	uint32_t sw;
	uint32_t sws;

	switch (osc) {
	case OSC_HSI:
		sw = STM32_RCC_CFGR_SW_HSI;
		sws = STM32_RCC_CFGR_SWS_HSI;
		break;
	case OSC_MSI:
		sw = STM32_RCC_CFGR_SW_MSI;
		sws = STM32_RCC_CFGR_SWS_MSI;
		break;
#ifdef STM32_HSE_CLOCK
	case OSC_HSE:
		sw = STM32_RCC_CFGR_SW_HSE;
		sws = STM32_RCC_CFGR_SWS_HSE;
		break;
#endif
	case OSC_PLL:
		sw = STM32_RCC_CFGR_SW_PLL;
		sws = STM32_RCC_CFGR_SWS_PLL;
		break;
	default:
		return;
	}

	STM32_RCC_CFGR = sw;
	while ((STM32_RCC_CFGR & STM32_RCC_CFGR_SWS_MASK) != sws)
		;
}

/*
 * Configure PLL for HSE
 *
 * 1. Disable the PLL by setting PLLON to 0 in RCC_CR.
 * 2. Wait until PLLRDY is cleared. The PLL is now fully stopped.
 * 3. Change the desired parameter.
 * 4. Enable the PLL again by setting PLLON to 1.
 * 5. Enable the desired PLL outputs by configuring PLLPEN, PLLQEN, PLLREN
 *    in RCC_PLLCFGR.
 */
static int stm32_configure_pll(enum clock_osc osc,
			       uint8_t m, uint8_t n, uint8_t r)
{
	uint32_t val;
	int f;

	/* 1 */
	STM32_RCC_CR &= ~STM32_RCC_CR_PLLON;

	/* 2 */
	while (STM32_RCC_CR & STM32_RCC_CR_PLLRDY)
		;

	/* 3 */
	val = STM32_RCC_PLLCFGR;

	val &= ~STM32_RCC_PLLCFGR_PLLSRC_MASK;
	switch (osc) {
	case OSC_HSI:
		val |= STM32_RCC_PLLCFGR_PLLSRC_HSI;
		f = STM32_HSI_CLOCK;
		break;
	case OSC_MSI:
		val |= STM32_RCC_PLLCFGR_PLLSRC_MSI;
		f = STM32_MSI_CLOCK;
		break;
#ifdef STM32_HSE_CLOCK
	case OSC_HSE:
		val |= STM32_RCC_PLLCFGR_PLLSRC_HSE;
		f = STM32_HSE_CLOCK;
		break;
#endif
	default:
		return -1;
	}

	ASSERT(m > 0 && m < 9);
	val &= ~STM32_RCC_PLLCFGR_PLLM_MASK;
	val |= (m  - 1) << STM32_RCC_PLLCFGR_PLLM_SHIFT;

	/* Max and min values are from TRM */
	ASSERT(n > 7 && n < 87);
	val &= ~STM32_RCC_PLLCFGR_PLLN_MASK;
	val |= n << STM32_RCC_PLLCFGR_PLLN_SHIFT;

	val &= ~STM32_RCC_PLLCFGR_PLLR_MASK;
	switch (r) {
	case 2:
		val |= 0 << STM32_RCC_PLLCFGR_PLLR_SHIFT;
		break;
	case 4:
		val |= 1 << STM32_RCC_PLLCFGR_PLLR_SHIFT;
		break;
	case 6:
		val |= 2 << STM32_RCC_PLLCFGR_PLLR_SHIFT;
		break;
	case 8:
		val |= 3 << STM32_RCC_PLLCFGR_PLLR_SHIFT;
		break;
	default:
		return -1;
	}

	STM32_RCC_PLLCFGR = val;

	/* 4 */
	clock_enable_osc(OSC_PLL);

	/* 5 */
	val = STM32_RCC_PLLCFGR;
	val |= 1 << STM32_RCC_PLLCFGR_PLLREN_SHIFT;
	STM32_RCC_PLLCFGR = val;

	/* (f * n) shouldn't overflow based on their max values */
	return (f * n / m / r);
}

/**
 * Set system clock oscillator
 *
 * @param osc		Oscillator to use
 * @param pll_osc	Source oscillator for PLL. Ignored if osc is not PLL.
 */
static void clock_set_osc(enum clock_osc osc, enum clock_osc pll_osc)
{
	uint32_t val;

	if (osc == current_osc)
		return;

	if (current_osc != OSC_INIT)
		hook_notify(HOOK_PRE_FREQ_CHANGE);

	switch (osc) {
	case OSC_HSI:
		/* Ensure that HSI is ON */
		clock_enable_osc(osc);

		/* Disable LPSDSR */
		STM32_PWR_CR &= ~STM32_PWR_CR_LPSDSR;

		/* Switch to HSI */
		clock_switch_osc(osc);

		/* Disable MSI */
		STM32_RCC_CR &= ~STM32_RCC_CR_MSION;

		freq = STM32_HSI_CLOCK;
		break;

	case OSC_MSI:
		/* Switch to MSI @ 1MHz */
		STM32_RCC_ICSCR =
			(STM32_RCC_ICSCR & ~STM32_RCC_ICSCR_MSIRANGE_MASK) |
			STM32_RCC_ICSCR_MSIRANGE_1MHZ;
		/* Ensure that MSI is ON */
		clock_enable_osc(osc);

		/* Switch to MSI */
		clock_switch_osc(osc);

		/* Disable HSI */
		STM32_RCC_CR &= ~STM32_RCC_CR_HSION;

		/* Enable LPSDSR */
		STM32_PWR_CR |= STM32_PWR_CR_LPSDSR;

		freq = STM32_MSI_CLOCK;
		break;

#ifdef STM32_HSE_CLOCK
	case OSC_HSE:
		/* Ensure that HSE is stable */
		clock_enable_osc(osc);

		/* Switch to HSE */
		clock_switch_osc(osc);

		/* Disable other clock sources */
		STM32_RCC_CR &= ~(STM32_RCC_CR_MSION | STM32_RCC_CR_HSION |
				STM32_RCC_CR_PLLON);

		freq = STM32_HSE_CLOCK;

		break;
#endif
	case OSC_PLL:
		/* Ensure that source clock is stable */
		clock_enable_osc(pll_osc);

		/* Configure PLLCFGR */
		freq = stm32_configure_pll(pll_osc, STM32_PLLM,
					   STM32_PLLN, STM32_PLLR);
		ASSERT(freq > 0);

		/* Adjust flash latency as instructed in TRM */
		val = STM32_FLASH_ACR;
		val &= ~STM32_FLASH_ACR_LATENCY_MASK;
		/* Flash 4 wait state. TODO: Should depend on freq. */
		val |= 4 << STM32_FLASH_ACR_LATENCY_SHIFT;
		STM32_FLASH_ACR = val;
		while (STM32_FLASH_ACR != val)
			;

		/* Switch to PLL */
		clock_switch_osc(osc);

		/* TODO: Disable other sources */
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
		new_mask = clock_mask | BIT(module);
	else
		new_mask = clock_mask & ~BIT(module);

	/* Only change clock if needed */
	if ((!!new_mask) != (!!clock_mask)) {

		/* Flush UART before switching clock speed */
		cflush();

		clock_set_osc(new_mask ? OSC_HSI : OSC_MSI, OSC_INIT);
	}

	clock_mask = new_mask;
}

void clock_init(void)
{
#ifdef STM32_HSE_CLOCK
	clock_set_osc(OSC_PLL, OSC_HSE);
#else
	clock_set_osc(OSC_HSI, OSC_INIT);
#endif
}

static void clock_chipset_startup(void)
{
	/* Return to full speed */
	clock_enable_module(MODULE_CHIPSET, 1);
}
DECLARE_HOOK(HOOK_CHIPSET_STARTUP, clock_chipset_startup, HOOK_PRIO_DEFAULT);
DECLARE_HOOK(HOOK_CHIPSET_RESUME, clock_chipset_startup, HOOK_PRIO_DEFAULT);

static void clock_chipset_shutdown(void)
{
	/* Drop to lower clock speed if no other module requires full speed */
	clock_enable_module(MODULE_CHIPSET, 0);
}
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, clock_chipset_shutdown, HOOK_PRIO_DEFAULT);
DECLARE_HOOK(HOOK_CHIPSET_SUSPEND, clock_chipset_shutdown, HOOK_PRIO_DEFAULT);

static int command_clock(int argc, char **argv)
{
	if (argc >= 2) {
		if (!strcasecmp(argv[1], "hsi"))
			clock_set_osc(OSC_HSI, OSC_INIT);
		else if (!strcasecmp(argv[1], "msi"))
			clock_set_osc(OSC_MSI, OSC_INIT);
#ifdef STM32_HSE_CLOCK
		else if (!strcasecmp(argv[1], "hse"))
			clock_set_osc(OSC_HSE, OSC_INIT);
		else if (!strcasecmp(argv[1], "pll"))
			clock_set_osc(OSC_PLL, OSC_HSE);
#else
		else if (!strcasecmp(argv[1], "pll"))
			clock_set_osc(OSC_PLL, OSC_HSI);
#endif
		else
			return EC_ERROR_PARAM1;
	}

	ccprintf("Clock frequency is now %d Hz\n", freq);
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(clock, command_clock,
			"hsi | msi"
#ifdef STM32_HSE_CLOCK
			" | hse | pll"
#endif
			,
			"Set clock frequency");
