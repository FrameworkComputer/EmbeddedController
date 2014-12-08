/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
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

#ifdef CONFIG_STM32L_FAKE_HIBERNATE
#include "extpower.h"
#include "keyboard_config.h"
#include "lid_switch.h"
#include "power.h"
#include "power_button.h"
#include "system.h"
#include "task.h"

static int fake_hibernate;
#endif

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

		/*
		 * Set the recommended flash settings for 16MHz clock.
		 *
		 * The 3 bits must be programmed strictly sequentially.
		 * Also, follow the RM to check 64-bit access and latency bit
		 * after writing those bits to the FLASH_ACR register.
		 */
		tmp_acr = STM32_FLASH_ACR;
		/* Enable 64-bit access */
		tmp_acr |= STM32_FLASH_ACR_ACC64;
		STM32_FLASH_ACR = tmp_acr;
		/* Check ACC64 bit == 1 */
		while (!(STM32_FLASH_ACR & STM32_FLASH_ACR_ACC64))
			;

		/* Enable Prefetch Buffer */
		tmp_acr |= STM32_FLASH_ACR_PRFTEN;
		STM32_FLASH_ACR = tmp_acr;

		/* Flash 1 wait state */
		tmp_acr |= STM32_FLASH_ACR_LATENCY;
		STM32_FLASH_ACR = tmp_acr;
		/* Check LATENCY bit == 1 */
		while (!(STM32_FLASH_ACR & STM32_FLASH_ACR_LATENCY))
			;

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

		/*
		 * Set the recommended flash settings for <= 2MHz clock.
		 *
		 * The 3 bits must be programmed strictly sequentially.
		 * Also, follow the RM to check 64-bit access and latency bit
		 * after writing those bits to the FLASH_ACR register.
		 */
		tmp_acr = STM32_FLASH_ACR;
		/* Flash 0 wait state */
		tmp_acr &= ~STM32_FLASH_ACR_LATENCY;
		STM32_FLASH_ACR = tmp_acr;
		/* Check LATENCY bit == 0 */
		while (STM32_FLASH_ACR & STM32_FLASH_ACR_LATENCY)
			;

		/* Disable prefetch Buffer */
		tmp_acr &= ~STM32_FLASH_ACR_PRFTEN;
		STM32_FLASH_ACR = tmp_acr;

		/* Disable 64-bit access */
		tmp_acr &= ~STM32_FLASH_ACR_ACC64;
		STM32_FLASH_ACR = tmp_acr;
		/* Check ACC64 bit == 0 */
		while (STM32_FLASH_ACR & STM32_FLASH_ACR_ACC64)
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

#ifdef CONFIG_STM32L_FAKE_HIBERNATE
/*
 *  This is for NOT having enough hibernate (more precisely, the stand-by mode)
 *  wake-up source pin. STM32L100 supports 3 wake-up source pins:
 *
 *     WKUP1 (PA0)  -- used for ACOK_PMU
 *     WKUP2 (PC13) -- used for LID_OPEN
 *     WKUP3 (PE6)  -- cannot be used due to IC package.
 *
 *  However, we need the power button as a wake-up source as well and there is
 *  no available pin for us (we don't want to move the ACOK_PMU pin).
 *
 *  Fortunately, the STM32L is low-power enough so that we don't need the
 *  super-low-power mode. So, we fake this hibernate mode and accept the
 *  following wake-up source.
 *
 *     RTC alarm  (faked as well).
 *     Power button
 *     Lid open
 *     AC detected
 *
 *  The original issue is here: crosbug.com/p/25435.
 */
void __enter_hibernate(uint32_t seconds, uint32_t microseconds)
{
	int i;
	fake_hibernate = 1;

#ifdef CONFIG_POWER_COMMON
	/*
	 * A quick hack to stop annoying messages from charger task.
	 *
	 * When the battery is under 3%, the power task would call
	 * power_off() to shutdown AP. However, the power_off() would
	 * notify the HOOK_CHIPSET_SHUTDOWN, where the last hook is
	 * charge_shutdown() and it hibernates the power task (infinite
	 * loop -- not real CPU hibernate mode). Unfortunately, the
	 * charger task is still running. It keeps generating annoying
	 * log message.
	 *
	 * Thus, the hack is to set the power state machine (before we
	 * enter infinite loop) so that the charger task thinks the AP
	 * is off and stops generating messages.
	 */
	power_set_state(POWER_G3);
#endif

	/*
	 * Change keyboard outputs to high-Z to reduce power draw.
	 * We don't need corresponding code to change them back,
	 * because fake hibernate is always exited with a reboot.
	 *
	 * A little hacky to do this here.
	 */
	for (i = GPIO_KB_OUT00; i < GPIO_KB_OUT00 + KEYBOARD_COLS; i++)
		gpio_set_flags(i, GPIO_INPUT);

	ccprints("fake hibernate. waits for power button/lid/RTC/AC");
	cflush();

	if (seconds || microseconds) {
		if (seconds)
			sleep(seconds);
		if (microseconds)
			usleep(microseconds);
	} else {
		while (1)
			task_wait_event(-1);
	}

	ccprints("fake RTC alarm fires. resets EC");
	cflush();
	system_reset(SYSTEM_RESET_HARD);
}

static void fake_hibernate_power_button_hook(void)
{
	if (fake_hibernate && lid_is_open() && !power_button_is_pressed()) {
		ccprints("%s() resets EC", __func__);
		cflush();
		system_reset(SYSTEM_RESET_HARD);
	}
}
DECLARE_HOOK(HOOK_POWER_BUTTON_CHANGE, fake_hibernate_power_button_hook,
	HOOK_PRIO_DEFAULT);

static void fake_hibernate_lid_hook(void)
{
	if (fake_hibernate && lid_is_open()) {
		ccprints("%s() resets EC", __func__);
		cflush();
		system_reset(SYSTEM_RESET_HARD);
	}
}
DECLARE_HOOK(HOOK_LID_CHANGE, fake_hibernate_lid_hook, HOOK_PRIO_DEFAULT);

static void fake_hibernate_ac_hook(void)
{
	if (fake_hibernate && extpower_is_present()) {
		ccprints("%s() resets EC", __func__);
		cflush();
		system_reset(SYSTEM_RESET_HARD);
	}
}
DECLARE_HOOK(HOOK_AC_CHANGE, fake_hibernate_ac_hook, HOOK_PRIO_DEFAULT);
#endif

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
