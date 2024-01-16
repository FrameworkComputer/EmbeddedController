/* Copyright 2017 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* System module driver depends on chip series for Chrome EC */
#include "builtin/assert.h"
#include "common.h"
#include "console.h"
#include "cpu.h"
#include "gpio.h"
#include "hwtimer_chip.h"
#include "mpu.h"
#include "registers.h"
#include "rom_chip.h"
#include "system.h"
#include "system_chip.h"
#include "task.h"
#include "util.h"

/* Begin address of Suspend RAM for hibernate utility */
uintptr_t __lpram_fw_start = CONFIG_LPRAM_BASE;
/* Offset of little FW in Suspend Ram for GDMA bypass */
#define LFW_OFFSET 0x160
/* Begin address of Suspend RAM for little FW (GDMA utilities). */
uintptr_t __lpram_lfw_start = CONFIG_LPRAM_BASE + LFW_OFFSET;

/*****************************************************************************/
/* IC specific low-level driver depends on chip series */

/**
 * Configure address 0x40001600 (Low Power RAM) in the the MPU
 * (Memory Protection Unit) as a "regular" memory
 */
void system_mpu_config(void)
{
	/* Enable MPU */
	CPU_MPU_CTRL = 0x7;

	/* Create a new MPU Region to allow execution from low-power ram */
	CPU_MPU_RNR = REGION_CHIP_RESERVED;
	CPU_MPU_RASR = CPU_MPU_RASR & 0xFFFFFFFE; /* Disable region */
	CPU_MPU_RBAR = CONFIG_LPRAM_BASE; /* Set region base address */
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
}

/**
 * hibernate function in low power ram for npcx5 series.
 */
__noreturn void __keep __attribute__((section(".lowpower_ram")))
__enter_hibernate_in_lpram(void)
{
	/*
	 * TODO (ML): Set stack pointer to upper 512B of Suspend RAM.
	 * Our bypass needs stack instructions but FW will turn off main ram
	 * later for better power consumption.
	 */
	asm("ldr r0, =0x40001800\n"
	    "mov sp, r0\n");

	/* Disable Code RAM first */
	SET_BIT(NPCX_PWDWN_CTL(NPCX_PMC_PWDWN_5), NPCX_PWDWN_CTL5_MRFSH_DIS);
	SET_BIT(NPCX_DISIDL_CTL, NPCX_DISIDL_CTL_RAM_DID);

	/* Set deep idle mode*/
	NPCX_PMCSR = 0x6;

	/* Enter deep idle, wake-up by GPIOs or RTC */
	/*
	 * TODO (ML): Although the probability is small, it still has chance
	 * to meet the same symptom that CPU's behavior is abnormal after
	 * wake-up from deep idle.
	 * Workaround: Apply the same bypass of idle but don't enable interrupt.
	 */
	asm("push {r0-r5}\n" /* Save needed registers */
	    "ldr r0, =0x40001600\n" /* Set r0 to Suspend RAM addr */
	    "wfi\n" /* Wait for int to enter idle */
	    "ldm r0, {r0-r5}\n" /* Add a delay after WFI */
	    "pop {r0-r5}\n" /* Restore regs before enabling ints */
	    "isb\n" /* Flush the cpu pipeline */
	);

	/* RTC wake-up */
	if (IS_BIT_SET(NPCX_WTC, NPCX_WTC_PTO))
		/*
		 * Mark wake-up reason for hibernate
		 * Do not call bbram_data_write directly cause of
		 * executing in low-power ram
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

/**
 * Hibernate function for different Nuvoton chip series.
 */
void __hibernate_npcx_series(void)
{
	int i;
	void (*__hibernate_in_lpram)(void) =
		(void (*)(void))(__lpram_fw_start | 0x01);

	/* Enable power for the Low Power RAM */
	CLEAR_BIT(NPCX_PWDWN_CTL(NPCX_PMC_PWDWN_6), 6);

	/* Enable Low Power RAM */
	NPCX_LPRAM_CTRL = 1;

	/* Copy the __enter_hibernate_in_lpram instructions to LPRAM */
	for (i = 0; i < &__flash_lpfw_end - &__flash_lpfw_start; i++)
		*((uint32_t *)__lpram_fw_start + i) =
			*(&__flash_lpfw_start + i);

	/* execute hibernate func in LPRAM */
	__hibernate_in_lpram();
}

#ifdef CONFIG_EXTERNAL_STORAGE
/* Sysjump utilities in low power ram for npcx5 series. */
__noreturn void __keep __attribute__((section(".lowpower_ram2")))
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
		(void (*)(uint32_t))(__lpram_lfw_start | 0x01);

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
