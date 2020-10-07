/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <stdnoreturn.h>

/* System module driver depends on chip series for Chrome EC */
#include "common.h"
#include "console.h"
#include "cpu.h"
#include "ec_commands.h"
#include "hooks.h"
#include "lct_chip.h"
#include "registers.h"
#include "system.h"
#include "task.h"
#include "util.h"
#include "gpio.h"
#include "hwtimer_chip.h"
#include "mpu.h"
#include "system_chip.h"
#include "rom_chip.h"

#define CPRINTS(format, args...) cprints(CC_SYSTEM, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_SYSTEM, format, ## args)

/* Macros for last 32K ram block */
#define LAST_RAM_BLK ((NPCX_RAM_SIZE / (32 * 1024)) - 1)
/* Higher bits are reserved and need to be masked */
#define RAM_PD_MASK  (~BIT(LAST_RAM_BLK))

#ifdef CONFIG_WORKAROUND_FLASH_DOWNLOAD_API
#define LFW_OFFSET 0x160
/* Begin address of Suspend RAM for little FW (GDMA utilities). */
uintptr_t __lpram_lfw_start = CONFIG_LPRAM_BASE + LFW_OFFSET;
#endif
/*****************************************************************************/
/* IC specific low-level driver depends on chip series */

/*
 * Configure address 0x40001600 (Low Power RAM) in the the MPU
 * (Memory Protection Unit) as a "regular" memory
 */
void system_mpu_config(void)
{
#ifdef CONFIG_WORKAROUND_FLASH_DOWNLOAD_API
	/*
	 * npcx9 Rev.1 has the problem for download_from_flash API.
	 * Workwaroud it by by the system_download_from_flash function
	 * in the suspend RAM like npcx5.
	 * TODO: Remove this when A2 chip is available
	 */
	/* Enable MPU */
	CPU_MPU_CTRL = 0x7;

	/* Create a new MPU Region to allow execution from low-power ram */
	CPU_MPU_RNR  = REGION_CHIP_RESERVED;
	CPU_MPU_RASR = CPU_MPU_RASR & 0xFFFFFFFE; /* Disable region */
	CPU_MPU_RBAR = CONFIG_LPRAM_BASE;         /* Set region base address */
	/*
	 * Set region size & attribute and enable region
	 * [31:29] - Reserved.
	 * [28]    - XN (Execute Never) = 0
	 * [27]    - Reserved.
	 * [26:24] - AP                 = 011 (Full access)
	 * [23:22] - Reserved.
	 * [21:19,18,17,16] - TEX,S,C,B = 001000 (Normal memory)
	 * [15:8]  - SRD                = 0 (Subregions enabled)
	 * [7:6]   - Reserved.
	 * [5:1]   - SIZE               = 01001 (1K)
	 * [0]     - ENABLE             = 1 (enabled)
	 */
	CPU_MPU_RASR = 0x03080013;
#endif
}

#ifdef CONFIG_HIBERNATE_PSL
#ifndef NPCX_PSL_MODE_SUPPORT
#error "Do not enable CONFIG_HIBERNATE_PSL if npcx ec doesn't support PSL mode!"
#endif

static enum psl_pin_t system_gpio_to_psl(enum gpio_signal signal)
{
	enum psl_pin_t psl_no;
	const struct gpio_info *g = gpio_list + signal;

	if (g->port == GPIO_PORT_D && g->mask == MASK_PIN2) /* GPIOD2 */
		psl_no = PSL_IN1;
	else if (g->port == GPIO_PORT_0 && (g->mask & 0x07)) /* GPIO00/01/02 */
		psl_no = GPIO_MASK_TO_NUM(g->mask) + 1;
	else
		psl_no = PSL_NONE;

	return psl_no;
}

#if NPCX_FAMILY_VERSION >= NPCX_FAMILY_NPCX9
void system_set_psl_gpo(int level)
{
	if (level)
		SET_BIT(NPCX_GLUE_PSL_MCTL1, NPCX_GLUE_PSL_MCTL1_PSL_GPO_CTL);
	else
		CLEAR_BIT(NPCX_GLUE_PSL_MCTL1, NPCX_GLUE_PSL_MCTL1_PSL_GPO_CTL);
}
#endif

void system_enter_psl_mode(void)
{
	/* Configure pins from GPIOs to PSL which rely on VSBY power rail. */
	gpio_config_module(MODULE_PMU, 1);

	/*
	 * In npcx7, only physical PSL_IN pins can pull PSL_OUT to high and
	 * reboot ec.
	 * In npcx9, LCT timeout event can also pull PSL_OUT.
	 * We won't decide the wake cause now but only mark we are entering
	 * hibernation via PSL.
	 * The actual wakeup cause will be checked by the PSL input event bits
	 * when ec reboots.
	 */
	NPCX_BBRAM(BBRM_DATA_INDEX_WAKE) = HIBERNATE_WAKE_PSL;

#if NPCX_FAMILY_VERSION >= NPCX_FAMILY_NPCX9
	 /*
	  * If pulse mode is enabled, the VCC power is turned off by the
	  * external component (Ex: PMIC) but PSL_OUT. So we can just return
	  * here.
	  */
	if (IS_BIT_SET(NPCX_GLUE_PSL_MCTL1, NPCX_GLUE_PSL_MCTL1_PLS_EN))
		return;
#endif

	/*
	 * Pull PSL_OUT (GPIO85) to low to cut off ec's VCC power rail by
	 * setting bit 5 of PDOUT(8).
	 */
	SET_BIT(NPCX_PDOUT(GPIO_PORT_8), 5);
}

/* Hibernate function implemented by PSL (Power Switch Logic) mode. */
noreturn void __keep __enter_hibernate_in_psl(void)
{
	system_enter_psl_mode();
	/* Spin and wait for PSL cuts power; should never return */
	while (1)
		;
}

static void system_psl_type_sel(enum psl_pin_t psl_pin, uint32_t flags)
{
	/* Set PSL input events' type as level or edge trigger */
	if ((flags & GPIO_INT_F_HIGH) || (flags & GPIO_INT_F_LOW))
		CLEAR_BIT(NPCX_GLUE_PSL_CTS, psl_pin + 4);
	else if ((flags & GPIO_INT_F_RISING) ||
		 (flags & GPIO_INT_F_FALLING))
		SET_BIT(NPCX_GLUE_PSL_CTS, psl_pin + 4);

	/*
	 * Set PSL input events' polarity is low (high-to-low) active or
	 * high (low-to-high) active
	 */
	if (flags & GPIO_HIB_WAKE_HIGH)
		SET_BIT(NPCX_DEVALT(ALT_GROUP_D), 2 * psl_pin);
	else
		CLEAR_BIT(NPCX_DEVALT(ALT_GROUP_D), 2 * psl_pin);
}

int system_config_psl_mode(enum gpio_signal signal)
{
	enum psl_pin_t psl_no;
	const struct gpio_info *g = gpio_list + signal;

	psl_no = system_gpio_to_psl(signal);
	if (psl_no == PSL_NONE)
		return 0;

	system_psl_type_sel(psl_no, g->flags);
	return 1;
}

#else
/**
 * Hibernate function in last 32K ram block for npcx7 series.
 * Do not use global variable since we also turn off data ram.
 */
noreturn void __keep __attribute__ ((section(".after_init")))
__enter_hibernate_in_last_block(void)
{
	/*
	 * The hibernate utility is located in the last block of RAM. The size
	 * of each RAM block is 32KB. We turn off all blocks except last one
	 * for better power consumption.
	 */
	NPCX_RAM_PD(0) = RAM_PD_MASK & 0xFF;
#if defined(CHIP_FAMILY_NPCX7)
	NPCX_RAM_PD(1) = (RAM_PD_MASK >> 8) & 0x0F;
#elif defined(CHIP_FAMILY_NPCX9)
	NPCX_RAM_PD(1) = (RAM_PD_MASK >> 8) & 0x7F;
#endif

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
#ifdef NPCX_LCT_SUPPORT
	else if (IS_BIT_SET(NPCX_LCTSTAT, NPCX_LCTSTAT_EVST)) {
		NPCX_BBRAM(BBRM_DATA_INDEX_WAKE) = HIBERNATE_WAKE_LCT;
		/* Clear LCT event */
		NPCX_LCTSTAT = BIT(NPCX_LCTSTAT_EVST);
	}
#endif
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
#if NPCX_FAMILY_VERSION >= NPCX_FAMILY_NPCX9
	CPRINTS("PSL_MCTL1 event: 0x%x", NPCX_GLUE_PSL_MCTL1 & 0x18);
#endif
}
DECLARE_HOOK(HOOK_INIT, report_psl_wake_source, HOOK_PRIO_DEFAULT);
#endif

/*
 * npcx9 Rev.1 has the problem for download_from_flash API.
 * Workwaroud it by executing the system_download_from_flash function
 * in the suspend RAM like npcx5.
 * TODO: Removing npcx9 when Rev.2 is available.
 */
#ifdef CONFIG_WORKAROUND_FLASH_DOWNLOAD_API
#ifdef CONFIG_EXTERNAL_STORAGE
/* Sysjump utilities in low power ram for npcx9 series. */
noreturn void __keep __attribute__ ((section(".lowpower_ram2")))
__start_gdma(uint32_t exeAddr)
{
	/* Enable GDMA now */
	SET_BIT(NPCX_GDMA_CTL, NPCX_GDMA_CTL_GDMAEN);

	/* Start GDMA */
	SET_BIT(NPCX_GDMA_CTL, NPCX_GDMA_CTL_SOFTREQ);

	/* Wait for transfer to complete/fail */
	while (!IS_BIT_SET(NPCX_GDMA_CTL, NPCX_GDMA_CTL_TC) &&
			!IS_BIT_SET(NPCX_GDMA_CTL, NPCX_GDMA_CTL_GDMAERR))
		;

	/* Disable GDMA now */
	CLEAR_BIT(NPCX_GDMA_CTL, NPCX_GDMA_CTL_GDMAEN);

	/*
	 * Failure occurs during GMDA transaction. Let watchdog issue and
	 * boot from RO region again.
	 */
	if (IS_BIT_SET(NPCX_GDMA_CTL, NPCX_GDMA_CTL_GDMAERR))
		while (1)
			;

	/*
	 * Jump to the exeAddr address if needed. Setting bit 0 of address to
	 * indicate it's a thumb branch for cortex-m series CPU.
	 */
	((void (*)(void))(exeAddr | 0x01))();

	/* Should never get here */
	while (1)
		;
}

/* Bypass for GMDA issue of ROM api utilities only on npcx5 series. */
void system_download_from_flash(uint32_t srcAddr, uint32_t dstAddr,
		uint32_t size, uint32_t exeAddr)
{
	int i;
	uint8_t chunkSize = 16; /* 4 data burst mode. ie.16 bytes */
	/*
	 * GDMA utility in Suspend RAM. Setting bit 0 of address to indicate
	 * it's a thumb branch for cortex-m series CPU.
	 */
	void (*__start_gdma_in_lpram)(uint32_t) =
			(void(*)(uint32_t))(__lpram_lfw_start | 0x01);

	/*
	 * Before enabling burst mode for better performance of GDMA, it's
	 * important to make sure srcAddr, dstAddr and size of transactions
	 * are 16 bytes aligned in case failure occurs.
	 */
	ASSERT((size % chunkSize) == 0 && (srcAddr % chunkSize) == 0 &&
			(dstAddr % chunkSize) == 0);

	/* Check valid address for jumpiing */
	ASSERT(exeAddr != 0x0);

	/* Enable power for the Low Power RAM */
	CLEAR_BIT(NPCX_PWDWN_CTL(NPCX_PMC_PWDWN_6), 6);

	/* Enable Low Power RAM */
	NPCX_LPRAM_CTRL = 1;

	/*
	 * Initialize GDMA for flash reading.
	 * [31:21] - Reserved.
	 * [20]    - GDMAERR   = 0  (Indicate GMDA transfer error)
	 * [19]    - Reserved.
	 * [18]    - TC        = 0  (Terminal Count. Indicate operation is end.)
	 * [17]    - Reserved.
	 * [16]    - SOFTREQ   = 0  (Don't trigger here)
	 * [15]    - DM        = 0  (Set normal demand mode)
	 * [14]    - Reserved.
	 * [13:12] - TWS.      = 10 (One double-word for every GDMA transaction)
	 * [11:10] - Reserved.
	 * [9]     - BME       = 1  (4-data ie.16 bytes - Burst mode enable)
	 * [8]     - SIEN      = 0  (Stop interrupt disable)
	 * [7]     - SAFIX     = 0  (Fixed source address)
	 * [6]     - Reserved.
	 * [5]     - SADIR     = 0  (Source address incremented)
	 * [4]     - DADIR     = 0  (Destination address incremented)
	 * [3:2]   - GDMAMS    = 00 (Software mode)
	 * [1]     - Reserved.
	 * [0]     - ENABLE    = 0  (Don't enable yet)
	 */
	NPCX_GDMA_CTL = 0x00002200;

	/* Set source base address */
	NPCX_GDMA_SRCB = CONFIG_MAPPED_STORAGE_BASE + srcAddr;

	/* Set destination base address */
	NPCX_GDMA_DSTB = dstAddr;

	/* Set number of transfers */
	NPCX_GDMA_TCNT = (size / chunkSize);

	/* Clear Transfer Complete event */
	SET_BIT(NPCX_GDMA_CTL, NPCX_GDMA_CTL_TC);

	/* Copy the __start_gdma_in_lpram instructions to LPRAM */
	for (i = 0; i < &__flash_lplfw_end - &__flash_lplfw_start; i++)
		*((uint32_t *)__lpram_lfw_start + i) =
				*(&__flash_lplfw_start + i);

	/* Start GDMA in Suspend RAM */
	__start_gdma_in_lpram(exeAddr);
}
#endif /* CONFIG_EXTERNAL_STORAGE */
#endif
