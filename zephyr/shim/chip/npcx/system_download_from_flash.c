/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include <stdnoreturn.h>

#include "common.h"
#include "system_chip.h"

/* Modules Map */
#define NPCX_GDMA_BASE_ADDR	0x40011000

/******************************************************************************/
/* GDMA (General DMA) Registers */
#define NPCX_GDMA_CTL		REG32(NPCX_GDMA_BASE_ADDR + 0x000)
#define NPCX_GDMA_SRCB		REG32(NPCX_GDMA_BASE_ADDR + 0x004)
#define NPCX_GDMA_DSTB		REG32(NPCX_GDMA_BASE_ADDR + 0x008)

/******************************************************************************/
/* GDMA register fields */
#define NPCX_GDMA_CTL_GDMAEN                         0
#define NPCX_GDMA_CTL_GDMAMS                         FIELD(2,   2)
#define NPCX_GDMA_CTL_DADIR                          4
#define NPCX_GDMA_CTL_SADIR                          5
#define NPCX_GDMA_CTL_SAFIX                          7
#define NPCX_GDMA_CTL_SIEN                           8
#define NPCX_GDMA_CTL_BME                            9
#define NPCX_GDMA_CTL_SBMS                           11
#define NPCX_GDMA_CTL_TWS                            FIELD(12,  2)
#define NPCX_GDMA_CTL_DM                             15
#define NPCX_GDMA_CTL_SOFTREQ                        16
#define NPCX_GDMA_CTL_TC                             18
#define NPCX_GDMA_CTL_GDMAERR                        20
#define NPCX_GDMA_CTL_BLOCK_BUG_CORRECTION_DISABLE   26

/* Sysjump utilities in low power ram for npcx series. */
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