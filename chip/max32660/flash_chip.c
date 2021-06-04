/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* MAX32660 Flash Memory Module for Chrome EC */

#include "flash.h"
#include "switch.h"
#include "system.h"
#include "timer.h"
#include "util.h"
#include "watchdog.h"
#include "registers.h"
#include "common.h"
#include "icc_regs.h"
#include "flc_regs.h"

#define CPUTS(outstr) cputs(CC_SYSTEM, outstr)
#define CPRINTS(format, args...) cprints(CC_SYSTEM, format, ##args)

/***** Definitions *****/

/// Bit mask that can be used to find the starting address of a page in flash
#define MXC_FLASH_PAGE_MASK ~(MXC_FLASH_PAGE_SIZE - 1)

/// Calculate the address of a page in flash from the page number
#define MXC_FLASH_PAGE_ADDR(page)                                              \
	(MXC_FLASH_MEM_BASE + ((unsigned long)page * MXC_FLASH_PAGE_SIZE))

void flash_operation(void)
{
	volatile uint32_t *line_addr;
	volatile uint32_t __attribute__((unused)) line;

	// Clear the cache
	MXC_ICC->cache_ctrl ^= MXC_F_ICC_CACHE_CTRL_CACHE_EN;
	MXC_ICC->cache_ctrl ^= MXC_F_ICC_CACHE_CTRL_CACHE_EN;

	// Clear the line fill buffer
	line_addr = (uint32_t *)(MXC_FLASH_MEM_BASE);
	line = *line_addr;

	line_addr = (uint32_t *)(MXC_FLASH_MEM_BASE + MXC_FLASH_PAGE_SIZE);
	line = *line_addr;
}

static int flash_busy(void)
{
	return (MXC_FLC->cn &
		(MXC_F_FLC_CN_WR | MXC_F_FLC_CN_ME | MXC_F_FLC_CN_PGE));
}

static int flash_init_controller(void)
{
	// Set flash clock divider to generate a 1MHz clock from the APB clock
	MXC_FLC->clkdiv = SystemCoreClock / 1000000;

	/* Check if the flash controller is busy */
	if (flash_busy()) {
		return EC_ERROR_BUSY;
	}

	/* Clear stale errors */
	if (MXC_FLC->intr & MXC_F_FLC_INTR_AF) {
		MXC_FLC->intr &= ~MXC_F_FLC_INTR_AF;
	}

	/* Unlock flash */
	MXC_FLC->cn = (MXC_FLC->cn & ~MXC_F_FLC_CN_UNLOCK) |
		      MXC_S_FLC_CN_UNLOCK_UNLOCKED;

	return EC_SUCCESS;
}

static int flash_device_page_erase(uint32_t address)
{
	int err;

	if ((err = flash_init_controller()) != EC_SUCCESS)
		return err;

	// Align address on page boundary
	address = address - (address % MXC_FLASH_PAGE_SIZE);

	/* Write paflash_init_controllerde */
	MXC_FLC->cn = (MXC_FLC->cn & ~MXC_F_FLC_CN_ERASE_CODE) |
		      MXC_S_FLC_CN_ERASE_CODE_ERASEPAGE;
	/* Issue page erase command */
	MXC_FLC->addr = address;
	MXC_FLC->cn |= MXC_F_FLC_CN_PGE;

	/* Wait until flash operation is complete */
	while (flash_busy())
		;

	/* Lock flash */
	MXC_FLC->cn &= ~MXC_F_FLC_CN_UNLOCK;

	/* Check access violations */
	if (MXC_FLC->intr & MXC_F_FLC_INTR_AF) {
		MXC_FLC->intr &= ~MXC_F_FLC_INTR_AF;
		return EC_ERROR_UNKNOWN;
	}

	flash_operation();

	return EC_SUCCESS;
}

int crec_flash_physical_write(int offset, int size, const char *data)
{
	int err;
	uint32_t bytes_written;
	uint8_t current_data[4];

	if ((err = flash_init_controller()) != EC_SUCCESS)
		return err;

	// write in 32-bit units until we are 128-bit aligned
	MXC_FLC->cn &= ~MXC_F_FLC_CN_BRST;
	MXC_FLC->cn |= MXC_F_FLC_CN_WDTH;

	// Align the address and read/write if we have to
	if (offset & 0x3) {

		// Figure out how many bytes we have to write to round up the
		// address
		bytes_written = 4 - (offset & 0x3);

		// Save the data currently in the flash
		memcpy(current_data, (void *)(offset & (~0x3)), 4);

		// Modify current_data to insert the data from buffer
		memcpy(&current_data[4 - bytes_written], data, bytes_written);

		// Write the modified data
		MXC_FLC->addr = offset - (offset % 4);
		memcpy((void *)&MXC_FLC->data[0], &current_data, 4);
		MXC_FLC->cn |= MXC_F_FLC_CN_WR;

		/* Wait until flash operation is complete */
		while (flash_busy())
			;

		offset += bytes_written;
		size -= bytes_written;
		data += bytes_written;
	}

	while ((size >= 4) && ((offset & 0x1F) != 0)) {
		MXC_FLC->addr = offset;
		memcpy((void *)&MXC_FLC->data[0], data, 4);
		MXC_FLC->cn |= MXC_F_FLC_CN_WR;

		/* Wait until flash operation is complete */
		while (flash_busy())
			;

		offset += 4;
		size -= 4;
		data += 4;
	}

	if (size >= 16) {

		// write in 128-bit bursts while we can
		MXC_FLC->cn &= ~MXC_F_FLC_CN_WDTH;

		while (size >= 16) {
			MXC_FLC->addr = offset;
			memcpy((void *)&MXC_FLC->data[0], data, 16);
			MXC_FLC->cn |= MXC_F_FLC_CN_WR;

			/* Wait until flash operation is complete */
			while (flash_busy())
				;

			offset += 16;
			size -= 16;
			data += 16;
		}

		// Return to 32-bit writes.
		MXC_FLC->cn |= MXC_F_FLC_CN_WDTH;
	}

	while (size >= 4) {
		MXC_FLC->addr = offset;
		memcpy((void *)&MXC_FLC->data[0], data, 4);
		MXC_FLC->cn |= MXC_F_FLC_CN_WR;

		/* Wait until flash operation is complete */
		while (flash_busy())
			;

		offset += 4;
		size -= 4;
		data += 4;
	}

	if (size > 0) {
		// Save the data currently in the flash
		memcpy(current_data, (void *)(offset), 4);

		// Modify current_data to insert the data from data
		memcpy(current_data, data, size);

		MXC_FLC->addr = offset;
		memcpy((void *)&MXC_FLC->data[0], current_data, 4);
		MXC_FLC->cn |= MXC_F_FLC_CN_WR;

		/* Wait until flash operation is complete */
		while (flash_busy())
			;
	}

	/* Lock flash */
	MXC_FLC->cn &= ~MXC_F_FLC_CN_UNLOCK;

	/* Check access violations */
	if (MXC_FLC->intr & MXC_F_FLC_INTR_AF) {
		MXC_FLC->intr &= ~MXC_F_FLC_INTR_AF;
		return EC_ERROR_UNKNOWN;
	}

	flash_operation();

	return EC_SUCCESS;
}

/*****************************************************************************/
/* Physical layer APIs */

int crec_flash_physical_erase(int offset, int size)
{
	int i;
	int pages;
	int error_status;

	/*
	 * erase 'size' number of bytes starting at address 'offset'
	 */
	/* calculate the number of pages */
	pages = size / CONFIG_FLASH_ERASE_SIZE;
	/* iterate over the number of pages */
	for (i = 0; i < pages; i++) {
		/* erase the page after calculating the start address */
		error_status = flash_device_page_erase(
			offset + (i * CONFIG_FLASH_ERASE_SIZE));
		if (error_status != EC_SUCCESS) {
			return error_status;
		}
	}
	return EC_SUCCESS;
}

int crec_flash_physical_get_protect(int bank)
{
	/* Not protected */
	return 0;
}

uint32_t crec_flash_physical_get_protect_flags(void)
{
	/* no flags set */
	return 0;
}

uint32_t crec_flash_physical_get_valid_flags(void)
{
	/* These are the flags we're going to pay attention to */
	return EC_FLASH_PROTECT_RO_AT_BOOT | EC_FLASH_PROTECT_RO_NOW |
	       EC_FLASH_PROTECT_ALL_NOW;
}

uint32_t crec_flash_physical_get_writable_flags(uint32_t cur_flags)
{
	/* no flags writable */
	return 0;
}

int crec_flash_physical_protect_at_boot(uint32_t new_flags)
{
	/* nothing to do here */
	return EC_SUCCESS;
}

int crec_flash_physical_protect_now(int all)
{
	/* nothing to do here */
	return EC_SUCCESS;
}

/*****************************************************************************/
/* High-level APIs */

int crec_flash_pre_init(void)
{
	return EC_SUCCESS;
}

/*****************************************************************************/
/* Test Commands */

/*
 * Read, Write, and Erase a page of flash memory using chip routines
 * NOTE: This is a DESTRUCTIVE test for the range of flash pages tested
 *       make sure that PAGE_START is beyond your flash code.
 */
static int command_flash_test1(int argc, char **argv)
{
	int i;
	uint8_t *ptr;
	const uint32_t PAGE_START = 9;
	const uint32_t PAGE_END = 32;
	uint32_t page;
	int error_status;
	uint32_t flash_address;
	const int BUFFER_SIZE = 32;
	uint8_t buffer[BUFFER_SIZE];

	/*
	 * As a test, write unique data to each page in this for loop, later
	 * verify data in pages
	 */
	for (page = PAGE_START; page < PAGE_END; page++) {
		flash_address = page * CONFIG_FLASH_ERASE_SIZE;

		/*
		 * erase page
		 */
		error_status = crec_flash_physical_erase(flash_address,
						    CONFIG_FLASH_ERASE_SIZE);
		if (error_status != EC_SUCCESS) {
			CPRINTS("Error with crec_flash_physical_erase\n");
			return EC_ERROR_UNKNOWN;
		}

		/*
		 * verify page was erased
		 */
		// CPRINTS("read flash page %d, address %x, ", page,
		// flash_address);
		ptr = (uint8_t *)flash_address;
		for (i = 0; i < CONFIG_FLASH_ERASE_SIZE; i++) {
			if (*ptr++ != 0xff) {
				CPRINTS("Error with verifying page erase\n");
				return EC_ERROR_UNKNOWN;
			}
		}

		/*
		 * write pattern to page, just write BUFFER_SIZE worth of data
		 */
		for (i = 0; i < BUFFER_SIZE; i++) {
			buffer[i] = i + page;
		}
		error_status = crec_flash_physical_write(flash_address,
							 BUFFER_SIZE, buffer);
		if (error_status != EC_SUCCESS) {
			CPRINTS("Error with crec_flash_physical_write\n");
			return EC_ERROR_UNKNOWN;
		}
	}

	/*
	 * Verify data in pages
	 */
	for (page = PAGE_START; page < PAGE_END; page++) {
		flash_address = page * CONFIG_FLASH_ERASE_SIZE;

		/*
		 * read a portion of flash memory
		 */
		ptr = (uint8_t *)flash_address;
		for (i = 0; i < BUFFER_SIZE; i++) {
			if (*ptr++ != (i + page)) {
				CPRINTS("Error with verifing written test "
					"data\n");
				return EC_ERROR_UNKNOWN;
			}
		}
		CPRINTS("Verified Erase, Write, Read page %d", page);
	}

	/*
	 * Clean up after tests
	 */
	for (page = PAGE_START; page <= PAGE_END; page++) {
		flash_address = page * CONFIG_FLASH_ERASE_SIZE;
		error_status = crec_flash_physical_erase(flash_address,
						    CONFIG_FLASH_ERASE_SIZE);
		if (error_status != EC_SUCCESS) {
			CPRINTS("Error with crec_flash_physical_erase\n");
			return EC_ERROR_UNKNOWN;
		}
	}

	CPRINTS("done command_flash_test1.");
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(flashtest1, command_flash_test1, "flashtest1",
			"Flash chip routine tests");
