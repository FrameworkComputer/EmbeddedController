/* Copyright 2014 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * NPCX SoC spi flash update tool - monitor firmware
 */

#include "config.h"
#include "npcx_monitor.h"
#include "registers.h"
#include "util.h"

#include <stdint.h>

/*
 * The FIU module version of NPCX4.
 * As npcx4 has some changes for the FIU module , it is not fully compatible
 * with the old FIU module. We need to read the FIU version of the chip in use
 * before any flash access.
 */
#define NPCX_FIU_VER_NUM_NPCX4 0x0C
/*
 * The base of the mapped address space for FIU0 in the chip families which have
 * only one FIU module.
 */
#define MAPPED_STORAGE_BASE_SINGLE_FIU_FIU0 0x64000000
/*
 * The base of the mapped address space for FIU0 in the chip families which have
 * more than one FIU modules.
 */
#define MAPPED_STORAGE_BASE_MULTI_FIU_FIU0 0x60000000

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
	NPCX_UMA_CTS = cts;
	while (IS_BIT_SET(NPCX_UMA_CTS, NPCX_UMA_CTS_EXEC_DONE))
		;
}

void sspi_flash_cs_level(int level)
{
	uint8_t sw_cs;

	if (NPCX_FIU_VER >= NPCX_FIU_VER_NUM_NPCX4) {
		sw_cs = NPCX_UMA_ECTS_SW_CS0;
	} else {
		sw_cs = NPCX_UMA_ECTS_SW_CS1;
	}
	/* level is high */
	if (level) {
		/* Set chip select to high */
		SET_BIT(NPCX_UMA_ECTS, sw_cs);
	} else { /* level is low */
		/* Set chip select to low */
		CLEAR_BIT(NPCX_UMA_ECTS, sw_cs);
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
		NPCX_UMA_CTS = MASK_RD_1BYTE;
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
	NPCX_UMA_DB0 = addr[2];
	NPCX_UMA_DB1 = addr[1];
	NPCX_UMA_DB2 = addr[0];
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
	sspi_flash_execute_cmd(CMD_FLASH_PROGRAM, MASK_CMD_WR_3BYTE);
	for (i = 0; i < bytes; i++) {
		sspi_flash_execute_cmd(*data, MASK_CMD_WR_ONLY);
		data++;
	}
	/* Chip Select up */
	sspi_flash_cs_level(1);
}

int sspi_flash_physical_clear_stsreg(void)
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

	/* Read status register 1/2 for checking */
	sspi_flash_execute_cmd(CMD_READ_STATUS_REG, MASK_CMD_RD_1BYTE);
	if (NPCX_UMA_DB0 != 0x00)
		return 0;
	sspi_flash_execute_cmd(CMD_READ_STATUS_REG2, MASK_CMD_RD_1BYTE);
	if (NPCX_UMA_DB0 != 0x00)
		return 0;
	/* Enable tri-state */
	sspi_flash_tristate(1);

	return 1;
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
	for (; size > 0; size -= NPCX_MONITOR_FLASH_ERASE_SIZE,
			 offset += NPCX_MONITOR_FLASH_ERASE_SIZE) {
		/* Enable write */
		sspi_flash_write_enable();
		/* Set erase address */
		sspi_flash_set_address(offset);
		/* Start erase */
		sspi_flash_execute_cmd(CMD_SECTOR_ERASE, MASK_CMD_WR_3BYTE);

		/* Wait erase completed */
		sspi_flash_wait_ready();
	}

	/* Enable tri-state */
	sspi_flash_tristate(1);
}

int sspi_flash_verify(int offset, int size, const char *data)
{
	int i, result;
	uint8_t *ptr_flash;
	uint8_t *ptr_mram;
	uint8_t cmp_data;

	if (NPCX_FIU_VER >= NPCX_FIU_VER_NUM_NPCX4) {
		ptr_flash = (uint8_t *)(MAPPED_STORAGE_BASE_MULTI_FIU_FIU0 +
					offset);
	} else {
		ptr_flash = (uint8_t *)(MAPPED_STORAGE_BASE_SINGLE_FIU_FIU0 +
					offset);
	}

	ptr_mram = (uint8_t *)data;
	result = 1;

	/* Disable tri-state */
	sspi_flash_tristate(0);

	/* Start to verify */
	for (i = 0; i < size; i++) {
		cmp_data = ptr_mram ? ptr_mram[i] : 0xFF;
		if (ptr_flash[i] != cmp_data) {
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
	int size = MAX(CONFIG_RO_SIZE, CONFIG_RW_SIZE); /* max size is 128KB */

	image = (const uint8_t *)fw_base;
	/*
	 * Scan backwards looking for 0xea byte, which is by definition the
	 * last byte of the image.  See ec.lds.S for how this is inserted at
	 * the end of the image.
	 */
	for (size--; size > 0 && image[size] != 0xea; size--)
		;

	return size ? size + 1 : 0; /* 0xea byte IS part of the image */
}

/* Entry function of spi upload function */
uint32_t __attribute__((section(".startup_text")))
sspi_flash_upload(int spi_offset, int spi_size)
{
	/*
	 * Flash image has been uploaded to Code RAM
	 */
	uint32_t sz_image;
	uint32_t uut_tag;
	const char *image_base;
	uint32_t *flag_upload = (uint32_t *)SPI_PROGRAMMING_FLAG;
	struct monitor_header_tag *monitor_header =
		(struct monitor_header_tag *)NPCX_MONITOR_HEADER_ADDR;

	*flag_upload = 0;

	uut_tag = monitor_header->tag;
	/* If it is UUT tag, read required parameters from header */
	if (uut_tag == NPCX_MONITOR_UUT_TAG) {
		sz_image = monitor_header->size;
		spi_offset = monitor_header->dest_addr;
		image_base = (const char *)(monitor_header->src_addr);
	} else {
		sz_image = spi_size;
		image_base = (const char *)CONFIG_PROGRAM_MEMORY_BASE;
	}

	/* Unlock & stop watchdog */
	NPCX_WDSDM = 0x87;
	NPCX_WDSDM = 0x61;
	NPCX_WDSDM = 0x63;

	/* UMA Unlock */
	CLEAR_BIT(NPCX_UMA_ECTS, NPCX_UMA_ECTS_UMA_LOCK);

	/*
	 * If UUT is used, assuming the target is the internal flash.
	 * Don't switch the pinmux and make sure bit 7 of DEVALT0 is set.
	 */
	if (uut_tag == NPCX_MONITOR_UUT_TAG)
		SET_BIT(NPCX_DEVALT(0), NPCX_DEVALT0_NO_F_SPI);
	else
		/* Set pinmux first */
		sspi_flash_pinmux(1);

	/*
	 * As we no loner use the ADDR field, set it to zero in case the
	 * defult value is not zero.
	 */
	SET_FIELD(NPCX_UMA_ECTS, NPCX_UMA_ECTS_UMA_ADDR_SIZE, 0);

	/* Get size of image automatically */
	if (sz_image == 0)
		sz_image = sspi_flash_get_image_used(image_base);

	/* Clear status reg of spi flash for protection */
	if (sspi_flash_physical_clear_stsreg()) {
		/* Start to erase */
		sspi_flash_physical_erase(spi_offset, sz_image);
		/* Start to write */
		if (image_base != NULL)
			sspi_flash_physical_write(spi_offset, sz_image,
						  image_base);
		/* Verify data */
		if (sspi_flash_verify(spi_offset, sz_image, image_base))
			*flag_upload |= 0x02;
	}
	if (uut_tag != NPCX_MONITOR_UUT_TAG)
		/* Disable pinmux */
		sspi_flash_pinmux(0);

	/* Mark we have finished upload work */
	*flag_upload |= 0x01;

	/* Return the status back to ROM code is required for UUT */
	if (uut_tag == NPCX_MONITOR_UUT_TAG)
		return *flag_upload;

	/* Infinite loop */
	for (;;)
		;
}
