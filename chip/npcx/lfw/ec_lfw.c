/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * NPCX5M5G SoC little FW used by booter
 */

#include <stdint.h>
#include "registers.h"
#include "config_chip.h"
#include "ec_lfw.h"

/* size of little FW */
#define LFW_SIZE        0x1000
/* signature used by booter */
#define SIG_GOOGLE_EC   0x55AA650E
/* little FW located on TOP of Flash - 4K */
#define FW_ADDR         (CONFIG_SPI_FLASH_SIZE - 0x1000)

/* Header used by NPCX5M5G Booter */
struct booter_header_t {
	uint32_t signature; /* A constant used to verify FW pointer is valid */
	uint32_t pointer_fw;/* Holds the BootLoader location in the flash */
};

__attribute__ ((section(".booter_pointer")))
const struct booter_header_t booter_header = {
	/* 00 */ SIG_GOOGLE_EC,
	/* 04 */ FW_ADDR
};


/* Original sp during sysjump */
uint32_t org_sp;

/*****************************************************************************/
/* flash internal functions */

void __attribute__ ((section(".instrucion_ram")))
flash_burst_copy_fw_to_mram(uint32_t addr_flash, uint32_t addr_mram,
		uint32_t size)
{
	uint32_t	bit32_idx;
	uint32_t	bit32_size;
	uint32_t	*bit32_ptr_mram;

	bit32_ptr_mram  = (uint32_t *)addr_mram;

	/* Round it up and get it in 4 bytes */
	bit32_size = (size+3) / 4;

	/* Set chip select to low */
	CLEAR_BIT(NPCX_UMA_ECTS, NPCX_UMA_ECTS_SW_CS1);

	/* Write flash address */
	NPCX_UMA_AB2 = (uint8_t)((addr_flash & 0xFF0000)>>16);
	NPCX_UMA_AB1 = (uint8_t)((addr_flash & 0xFF00)>>8);
	NPCX_UMA_AB0 = (uint8_t)((addr_flash & 0xFF));

	NPCX_UMA_CODE = CMD_FAST_READ;
	NPCX_UMA_CTS  = MASK_CMD_ADR_WR;
	/* wait for UMA to complete */
	while (IS_BIT_SET(NPCX_UMA_CTS, EXEC_DONE))
		;

	/* Start to burst read and copy data to Code RAM */
	for (bit32_idx = 0; bit32_idx < bit32_size; bit32_idx++) {
		/* 1101 0100 - EXEC, RD, NO CMD, NO ADDR, 4 bytes */
		NPCX_UMA_CTS  = MASK_RD_4BYTE;
		while (IS_BIT_SET(NPCX_UMA_CTS, EXEC_DONE))
			;
		/* copy data to Code RAM */
		bit32_ptr_mram[bit32_idx] = NPCX_UMA_DB0_3;
	}

	/* Set chip select to high */
	SET_BIT(NPCX_UMA_ECTS, NPCX_UMA_ECTS_SW_CS1);
}

void __attribute__ ((section(".instrucion_ram")))
bin2ram(void)
{
	/* copy image from RO base */
	if (IS_BIT_SET(NPCX_FWCTRL, NPCX_FWCTRL_RO_REGION))
		flash_burst_copy_fw_to_mram(CONFIG_FW_RO_OFF, CONFIG_CDRAM_BASE,
				CONFIG_FW_RO_SIZE - LFW_SIZE);
	/* copy image from RW base */
	else
		flash_burst_copy_fw_to_mram(CONFIG_FW_RW_OFF, CONFIG_CDRAM_BASE,
				CONFIG_FW_RW_SIZE - LFW_SIZE);

	/* Disable FIU pins to tri-state */
	CLEAR_BIT(NPCX_DEVCNT, NPCX_DEVCNT_F_SPI_TRIS);

	/* TODO: (ML) Booter has cleared watchdog flag */
#ifndef CHIP_NPCX5M5G
	static uint32_t reset_flag;
	/* Check for VCC1 reset */
	if (IS_BIT_SET(NPCX_RSTCTL, NPCX_RSTCTL_VCC1_RST_STS)) {
		/* Clear flag bit */
		SET_BIT(NPCX_RSTCTL, NPCX_RSTCTL_VCC1_RST_STS);
		reset_flag = 1;
	}
	/* Software debugger reset */
	else if (IS_BIT_SET(NPCX_RSTCTL, NPCX_RSTCTL_DBGRST_STS))
		reset_flag = 1;
	/* Watchdog Reset */
	else if (IS_BIT_SET(NPCX_T0CSR, NPCX_T0CSR_WDRST_STS)) {
		reset_flag = 1;
	} else {
		reset_flag = 0;
	}

	if (reset_flag) {
#else
	/* Workaround method to distinguish reboot or sysjump */
	if (org_sp < 0x200C0000) {
#endif
		/* restore sp from begin of RO image */
		asm volatile("ldr r0, =0x10088000\n"
					 "ldr r1, [r0]\n"
					 "mov sp, r1\n");
	} else {
		/* restore sp from sysjump */
		asm volatile("mov sp, %0" : : "r"(org_sp));
	}

	/* Jump to reset ISR */
	asm volatile(
		"ldr r0, =0x10088004\n"
		"ldr r1, [r0]\n"
		"mov pc, r1\n");
}

/* Entry function of little FW */
void __attribute__ ((section(".startup_text"), noreturn))
entry_lfw(void)
{
	uint32_t i;

	/* Backup sp value */
	asm volatile("mov %0, sp" : "=r"(org_sp));
	/* initialize sp with Data RAM */
	asm volatile(
			"ldr r0, =0x100A8000\n"
			"mov sp, r0\n");

	/* Copy the bin2ram code to RAM */
	for (i = 0; i < &__iram_fw_end - &__iram_fw_start; i++)
		*(&__iram_fw_start + i) = *(&__flash_fw_start + i);

	/* Run code in RAM */
	bin2ram();

	/* Should never reach this */
	for (;;)
		;
}
