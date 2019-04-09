/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* System module driver depends on chip series for Chrome EC */
#include "common.h"
#include "console.h"
#include "cpu.h"
#include "ec_commands.h"
#include "hooks.h"
#include "registers.h"
#include "system.h"
#include "task.h"
#include "util.h"
#include "gpio.h"
#include "hwtimer_chip.h"
#include "system_chip.h"
#include "rom_chip.h"

#define CPRINTS(format, args...) cprints(CC_SYSTEM, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_SYSTEM, format, ## args)

/* Macros for last 32K ram block */
#define LAST_RAM_BLK ((NPCX_RAM_SIZE / (32 * 1024)) - 1)
#define RAM_PD_MASK  (BIT(LAST_RAM_BLK) - 1)

/*****************************************************************************/
/* IC specific low-level driver depends on chip series */

void system_mpu_config(void)
{
}

#ifdef CONFIG_HIBERNATE_PSL
#ifndef NPCX_PSL_MODE_SUPPORT
#error "Do not enable CONFIG_HIBERNATE_PSL if npcx ec doesn't support PSL mode!"
#endif

void system_enter_psl_mode(void)
{
	/* Configure pins from GPIOs to PSL which rely on VSBY power rail. */
	gpio_config_module(MODULE_PMU, 1);

	/*
	 * Only PSL_IN events can pull PSL_OUT to high and reboot ec.
	 * We should treat it as wake-up pin reset.
	 */
	NPCX_BBRAM(BBRM_DATA_INDEX_WAKE) = HIBERNATE_WAKE_PIN;

	/*
	 * Pull PSL_OUT (GPIO85) to low to cut off ec's VCC power rail by
	 * setting bit 5 of PDOUT(8).
	 */
	SET_BIT(NPCX_PDOUT(GPIO_PORT_8), 5);
}

/* Hibernate function implemented by PSL (Power Switch Logic) mode. */
void __keep __attribute__ ((noreturn)) __enter_hibernate_in_psl(void)
{
	system_enter_psl_mode();
	/* Spin and wait for PSL cuts power; should never return */
	while (1)
		;
}

static void system_psl_type_sel(int psl_no, uint32_t flags)
{
	/* Set PSL input events' type as level or edge trigger */
	if ((flags & GPIO_INT_F_HIGH) || (flags & GPIO_INT_F_LOW))
		CLEAR_BIT(NPCX_GLUE_PSL_CTS, psl_no + 4);
	else if ((flags & GPIO_INT_F_RISING) || (flags & GPIO_INT_F_FALLING))
		SET_BIT(NPCX_GLUE_PSL_CTS, psl_no + 4);

	/*
	 * Set PSL input events' polarity is low (high-to-low) active or
	 * high (low-to-high) active
	 */
	if (flags & GPIO_HIB_WAKE_HIGH)
		SET_BIT(NPCX_DEVALT(ALT_GROUP_D), 2 * psl_no);
	else
		CLEAR_BIT(NPCX_DEVALT(ALT_GROUP_D), 2 * psl_no);
}

int system_config_psl_mode(enum gpio_signal signal)
{
	int psl_no;
	const struct gpio_info *g = gpio_list + signal;

	if (g->port == GPIO_PORT_D && g->mask == MASK_PIN2) /* GPIOD2 */
		psl_no = 0;
	else if (g->port == GPIO_PORT_0 && (g->mask & 0x07)) /* GPIO00/01/02 */
		psl_no = GPIO_MASK_TO_NUM(g->mask) + 1;
	else
		return 0;

	system_psl_type_sel(psl_no, g->flags);
	return 1;
}

#else
/**
 * Hibernate function in last 32K ram block for npcx7 series.
 * Do not use global variable since we also turn off data ram.
 */
void __keep __attribute__ ((noreturn, section(".after_init")))
__enter_hibernate_in_last_block(void)
{
	/*
	 * The hibernate utility is located in the last block of RAM. The size
	 * of each RAM block is 32KB. We turn off all blocks except last one
	 * for better power consumption.
	 */
	NPCX_RAM_PD(0) = RAM_PD_MASK & 0xFF;
	NPCX_RAM_PD(1) = RAM_PD_MASK >> 8;

	/* Set deep idle mode */
	NPCX_PMCSR = 0x6;

	/* Enter deep idle, wake-up by GPIOs or RTC */
	asm volatile ("wfi");

	/* RTC wake-up */
	if (IS_BIT_SET(NPCX_WTC, NPCX_WTC_PTO))
		/*
		 * Mark wake-up reason for hibernate
		 * Do not call bbram_data_write directly cause of
		 * no stack.
		 */
		NPCX_BBRAM(BBRM_DATA_INDEX_WAKE) = HIBERNATE_WAKE_MTC;
	else
		/* Otherwise, we treat it as GPIOs wake-up */
		NPCX_BBRAM(BBRM_DATA_INDEX_WAKE) = HIBERNATE_WAKE_PIN;

	/* Start a watchdog reset */
	NPCX_WDCNT = 0x01;
	/* Reload and restart Timer 0 */
	SET_BIT(NPCX_T0CSR, NPCX_T0CSR_RST);
	/* Wait for timer is loaded and restart */
	while (IS_BIT_SET(NPCX_T0CSR, NPCX_T0CSR_RST))
		;

	/* Spin and wait for reboot; should never return */
	while (1)
		;
}
#endif

/**
 * Hibernate function for different Nuvoton chip series.
 */
void __hibernate_npcx_series(void)
{
#ifdef CONFIG_HIBERNATE_PSL
	__enter_hibernate_in_psl();
#else
	/* Make sure this is located in the last 32K code RAM block */
	ASSERT((uint32_t)(&__after_init_end) - CONFIG_PROGRAM_MEMORY_BASE
								< (32*1024));

	/* Execute hibernate func in last 32K block */
	__enter_hibernate_in_last_block();
#endif
}

#if defined(CONFIG_HIBERNATE_PSL)
static void report_psl_wake_source(void)
{
	if (!(system_get_reset_flags() & EC_RESET_FLAG_HIBERNATE))
		return;

	CPRINTS("PSL_CTS: 0x%x", NPCX_GLUE_PSL_CTS & 0xf);
}
DECLARE_HOOK(HOOK_INIT, report_psl_wake_source, HOOK_PRIO_DEFAULT);
#endif
