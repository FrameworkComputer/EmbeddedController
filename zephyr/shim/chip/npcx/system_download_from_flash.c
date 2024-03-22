/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "common.h"
#include "soc.h"
#include "system_chip.h"

#include <zephyr/dt-bindings/clock/npcx_clock.h>
#include <zephyr/sys/__assert.h>
#include <zephyr/toolchain.h>

/* Modules Map */
#define NPCX_PMC_BASE_ADDR 0x4000D000
#define NPCX_GDMA_BASE_ADDR 0x40011000

/******************************************************************************/
/* GDMA (General DMA) Registers */
#define NPCX_GDMA_CTL REG32(NPCX_GDMA_BASE_ADDR + 0x000)
#define NPCX_GDMA_SRCB REG32(NPCX_GDMA_BASE_ADDR + 0x004)
#define NPCX_GDMA_DSTB REG32(NPCX_GDMA_BASE_ADDR + 0x008)
#define NPCX_GDMA_TCNT REG32(NPCX_GDMA_BASE_ADDR + 0x00C)

/******************************************************************************/
/* GDMA register fields */
#define NPCX_GDMA_CTL_GDMAEN 0
#define NPCX_GDMA_CTL_GDMAMS FIELD(2, 2)
#define NPCX_GDMA_CTL_DADIR 4
#define NPCX_GDMA_CTL_SADIR 5
#define NPCX_GDMA_CTL_SAFIX 7
#define NPCX_GDMA_CTL_SIEN 8
#define NPCX_GDMA_CTL_BME 9
#define NPCX_GDMA_CTL_SBMS 11
#define NPCX_GDMA_CTL_TWS FIELD(12, 2)
#define NPCX_GDMA_CTL_DM 15
#define NPCX_GDMA_CTL_SOFTREQ 16
#define NPCX_GDMA_CTL_TC 18
#define NPCX_GDMA_CTL_GDMAERR 20
#define NPCX_GDMA_CTL_BLOCK_BUG_CORRECTION_DISABLE 26

/******************************************************************************/
/* Low Power RAM definitions */
#define NPCX_LPRAM_CTRL REG32(0x40001044)

/******************************************************************************/
/* Sysjump utilities in low power ram for npcx series. */
FUNC_NORETURN void __keep __attribute__((section(".lowpower_ram2")))
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

/* Begin address of Suspend RAM for little FW (GDMA utilities). */
#define LFW_OFFSET 0x160
uintptr_t __lpram_lfw_start = CONFIG_LPRAM_BASE + LFW_OFFSET;

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
	__ASSERT_NO_MSG((size % chunkSize) == 0 && (srcAddr % chunkSize) == 0 &&
			(dstAddr % chunkSize) == 0);

	/* Check valid address for jumpiing */
	__ASSERT_NO_MSG(exeAddr != 0x0);

	/* Enable power for the Low Power RAM */
	CLEAR_BIT(NPCX_PWDWN_CTL(NPCX_PMC_BASE_ADDR, NPCX_PWDWN_CTL6), 6);

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
