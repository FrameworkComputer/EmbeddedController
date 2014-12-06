/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * NPCX5M5G SoC spi flash update tool
 */

#include <stdint.h>
#include "registers.h"
#include "config_chip.h"

/*****************************************************************************/
/* spi flash internal functions */
void sspi_flash_pinmux(int enable)
{
	if (enable)
		CLEAR_BIT(NPCX_DEVALT(0), NPCX_DEVALT0_NO_F_SPI);
	else
		SET_BIT(NPCX_DEVALT(0), NPCX_DEVALT0_NO_F_SPI);

	/* CS0/1 pinmux */
	if (enable) {
#if (FIU_CHIP_SELECT == 1)
		SET_BIT(NPCX_DEVALT(0), NPCX_DEVALT0_F_SPI_CS1_1);
#elif (FIU_CHIP_SELECT == 2)
		SET_BIT(NPCX_DEVALT(0), NPCX_DEVALT0_F_SPI_CS1_2);
#endif
	} else {
		CLEAR_BIT(NPCX_DEVALT(0), NPCX_DEVALT0_F_SPI_CS1_1);
		CLEAR_BIT(NPCX_DEVALT(0), NPCX_DEVALT0_F_SPI_CS1_2);
	}
}

void sspi_flash_tristate(int enable)
{
	if (enable) {
		/* Enable FIU pins to tri-state */
		SET_BIT(NPCX_DEVCNT, NPCX_DEVCNT_F_SPI_TRIS);
	} else {
		/* Disable FIU pins to tri-state */
		CLEAR_BIT(NPCX_DEVCNT, NPCX_DEVCNT_F_SPI_TRIS);
	}
}

void sspi_flash_execute_cmd(uint8_t code, uint8_t cts)
{
	/* set UMA_CODE */
	NPCX_UMA_CODE = code;
	/* execute UMA flash transaction */
	NPCX_UMA_CTS  = cts;
	while (IS_BIT_SET(NPCX_UMA_CTS, NPCX_UMA_CTS_EXEC_DONE))
		;
}

void sspi_flash_cs_level(int level)
{
	/* level is high */
	if (level) {
		/* Set chip select to high */
		SET_BIT(NPCX_UMA_ECTS, NPCX_UMA_ECTS_SW_CS1);
	} else { /* level is low */
		/* Set chip select to low */
		CLEAR_BIT(NPCX_UMA_ECTS, NPCX_UMA_ECTS_SW_CS1);
	}
}

void sspi_flash_wait_ready(void)
{
	uint8_t mask = SPI_FLASH_SR1_BUSY;

	/* Chip Select down. */
	sspi_flash_cs_level(0);
	/* Command for Read status register */
	sspi_flash_execute_cmd(CMD_READ_STATUS_REG, MASK_CMD_ONLY);
	do {
		/* Read status register */
		NPCX_UMA_CTS  = MASK_RD_1BYTE;
		while (IS_BIT_SET(NPCX_UMA_CTS, NPCX_UMA_CTS_EXEC_DONE))
			;
	} while (NPCX_UMA_DB0 & mask); /* Wait for Busy clear */
	/* Chip Select high. */
	sspi_flash_cs_level(1);
}

int sspi_flash_write_enable(void)
{
	uint8_t mask = SPI_FLASH_SR1_WEL;
	/* Write enable command */
	sspi_flash_execute_cmd(CMD_WRITE_EN, MASK_CMD_ONLY);
	/* Wait for flash is not busy */
	sspi_flash_wait_ready();

	if (NPCX_UMA_DB0 & mask)
		return 1;
	else
		return 0;
}

void sspi_flash_set_address(uint32_t dest_addr)
{
	uint8_t *addr = (uint8_t *)&dest_addr;
	/* Write address */
	NPCX_UMA_AB2 = addr[2];
	NPCX_UMA_AB1 = addr[1];
	NPCX_UMA_AB0 = addr[0];
}

void sspi_flash_burst_write(unsigned int dest_addr, unsigned int bytes,
		const char *data)
{
	unsigned int i;
	/* Chip Select down. */
	sspi_flash_cs_level(0);
	/* Set erase address */
	sspi_flash_set_address(dest_addr);
	/* Start write */
	sspi_flash_execute_cmd(CMD_FLASH_PROGRAM, MASK_CMD_WR_ADR);
	for (i = 0; i < bytes; i++) {
		sspi_flash_execute_cmd(*data, MASK_CMD_WR_ONLY);
		data++;
	}
	/* Chip Select up */
	sspi_flash_cs_level(1);
}

void sspi_flash_physical_clear_stsreg(void)
{
	/* Disable tri-state */
	sspi_flash_tristate(0);
	/* Enable write */
	sspi_flash_write_enable();

	NPCX_UMA_DB0 = 0x0;
	NPCX_UMA_DB1 = 0x0;

	/* Write status register 1/2 */
	sspi_flash_execute_cmd(CMD_WRITE_STATUS_REG, MASK_CMD_WR_2BYTE);

	/* Wait writing completed */
	sspi_flash_wait_ready();

	/* Read status register 1/2 */
	sspi_flash_execute_cmd(CMD_READ_STATUS_REG, MASK_CMD_RD_1BYTE);
	sspi_flash_execute_cmd(CMD_READ_STATUS_REG2, MASK_CMD_RD_1BYTE);
	/* Enable tri-state */
	sspi_flash_tristate(1);
}

void sspi_flash_physical_write(int offset, int size, const char *data)
{
	int dest_addr = offset;
	const int sz_page = CONFIG_FLASH_WRITE_IDEAL_SIZE;

	/* Disable tri-state */
	sspi_flash_tristate(0);

	/* Write the data per CONFIG_FLASH_WRITE_IDEAL_SIZE bytes */
	for (; size >= sz_page; size -= sz_page) {
		/* Enable write */
		sspi_flash_write_enable();
		/* Burst UMA transaction */
		sspi_flash_burst_write(dest_addr, sz_page, data);
		/* Wait write completed */
		sspi_flash_wait_ready();

		data += sz_page;
		dest_addr += sz_page;
	}

	/* Handle final partial page, if any */
	if (size != 0) {
		/* Enable write */
		sspi_flash_write_enable();
		/* Burst UMA transaction */
		sspi_flash_burst_write(dest_addr, size, data);

		/* Wait write completed */
		sspi_flash_wait_ready();
	}

	/* Enable tri-state */
	sspi_flash_tristate(1);
}

void sspi_flash_physical_erase(int offset, int size)
{
	/* Disable tri-state */
	sspi_flash_tristate(0);

	/* Alignment has been checked in upper layer */
	for (; size > 0; size -= CONFIG_FLASH_ERASE_SIZE,
		offset += CONFIG_FLASH_ERASE_SIZE) {
		/* Enable write */
		sspi_flash_write_enable();
		/* Set erase address */
		sspi_flash_set_address(offset);
		/* Start erase */
		sspi_flash_execute_cmd(CMD_SECTOR_ERASE, MASK_CMD_ADR);

		/* Wait erase completed */
		sspi_flash_wait_ready();
	}

	/* Enable tri-state */
	sspi_flash_tristate(1);
}

/* Start to write */
int sspi_flash_verify(int offset, int size, const char *data)
{
	int		i, result;
	uint8_t		*ptr_flash;
	uint8_t		*ptr_mram;

	ptr_flash = (uint8_t *)(0x64000000+offset);
	ptr_mram  = (uint8_t *)data;
	result = 1;

	/* Disable tri-state */
	sspi_flash_tristate(0);

	/* Start to verify */
	for (i = 0; i < size; i++) {
		if (ptr_flash[i] != ptr_mram[i]) {
			result = 0;
			break;
		}
	}

	/* Enable tri-state */
	sspi_flash_tristate(1);
	return result;
}

int sspi_flash_get_image_used(const char *fw_base)
{
	const uint8_t *image;
	int size = 0x20000; /* maximum size is 128KB */

	image = (const uint8_t *)fw_base;
	/*
	 * Scan backwards looking for 0xea byte, which is by definition the
	 * last byte of the image.  See ec.lds.S for how this is inserted at
	 * the end of the image.
	 */
	for (size--; size > 0 && image[size] != 0xea; size--)
		;

	return size ? size + 1 : 0;  /* 0xea byte IS part of the image */

}

volatile __attribute__((section(".up_flag"))) unsigned int flag_upload;

/* Entry function of spi upload function */
void __attribute__ ((section(".startup_text"), noreturn))
sspi_flash_upload(int spi_offset, int spi_size)
{
	/*
	 * Flash image has been uploaded to Code RAM
	 */
	const char *image_base = (char *)0x10088000;
	uint32_t sz_image = spi_size;

	/* Set pinmux first */
	sspi_flash_pinmux(1);

	/* Get size of image automatically */
	if (sz_image == 0)
		sz_image = sspi_flash_get_image_used(image_base);

	/* Clear status reg of spi flash for protection */
	sspi_flash_physical_clear_stsreg();

	/* Start to erase */
	sspi_flash_physical_erase(spi_offset, sz_image);

	/* Start to write */
	sspi_flash_physical_write(spi_offset, sz_image, image_base);

	/* Verify data */
	if (sspi_flash_verify(spi_offset, sz_image, image_base))
		flag_upload |= 0x02;

	/* Disable pinmux */
	sspi_flash_pinmux(0);

	/* Mark we have finished upload work */
	flag_upload |= 0x01;

	/* Should never reach this*/
	for (;;)
		;
}

