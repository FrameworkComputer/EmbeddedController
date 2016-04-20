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

int clock_get_freq(void)
{
	return freq;
}

void clock_wait_bus_cycles(enum bus_type bus, uint32_t cycles)
{
	volatile uint32_t dummy __attribute__((unused));

	if (bus == BUS_AHB) {
		while (cycles--)
			dummy = STM32_DMA1_REGS->isr;
	} else { /* APB */
		while (cycles--)
			dummy = STM32_USART_BRR(STM32_USART1_BASE);
	}
}

/**
 * Set which oscillator is used for the clock
 *
 * @param osc		Oscillator to use
 */
static void clock_set_osc(enum clock_osc osc)
{
	if (osc == current_osc)
		return;

	if (current_osc != OSC_INIT)
		hook_notify(HOOK_PRE_FREQ_CHANGE);

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

		/* Switch to HSI */
		STM32_RCC_CFGR = STM32_RCC_CFGR_SW_HSI;
		/* RM says to check SWS bits to make sure HSI is the sysclock */
		while ((STM32_RCC_CFGR & STM32_RCC_CFGR_SWS_MASK) !=
			STM32_RCC_CFGR_SWS_HSI)
			;

		/* Disable MSI */
		STM32_RCC_CR &= ~STM32_RCC_CR_MSION;

		freq = HSI_CLOCK;
		break;

	case OSC_MSI:
		/* Switch to MSI @ 1MHz */
		STM32_RCC_ICSCR =
			(STM32_RCC_ICSCR & ~STM32_RCC_ICSCR_MSIRANGE_MASK) |
			STM32_RCC_ICSCR_MSIRANGE_1MHZ;
		/* Ensure that MSI is ON */
		if (!(STM32_RCC_CR & STM32_RCC_CR_MSIRDY)) {
			/* Enable MSI */
			STM32_RCC_CR |= STM32_RCC_CR_MSION;
			/* Wait for MSI to be ready */
			while (!(STM32_RCC_CR & STM32_RCC_CR_MSIRDY))
				;
		}

		/* Switch to MSI */
		STM32_RCC_CFGR = STM32_RCC_CFGR_SW_MSI;
		/* RM says to check SWS bits to make sure MSI is the sysclock */
		while ((STM32_RCC_CFGR & STM32_RCC_CFGR_SWS_MASK) !=
			STM32_RCC_CFGR_SWS_MSI)
			;

		/* Disable HSI */
		STM32_RCC_CR &= ~STM32_RCC_CR_HSION;

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

		clock_set_osc(new_mask ? OSC_HSI : OSC_MSI);
	}

	clock_mask = new_mask;
}

void clock_init(void)
{
	/*
	 * The initial state :
	 *  SYSCLK from MSI (=2MHz), no divider on AHB, APB1, APB2
	 *  PLL unlocked, RTC enabled on LSE
	 */

	/* Switch to high-speed oscillator */
	clock_set_osc(OSC_HSI);
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
