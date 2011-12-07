/* Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Flash memory module for Chrome EC */

#include "flash.h"
#include "gpio.h"
#include "uart.h"
#include "registers.h"
#include "util.h"


static int usable_flash_size;


int flash_get_size(void)
{
	return usable_flash_size;
}


int flash_get_write_block_size(void)
{
	return FLASH_WRITE_BYTES;
}


int flash_get_erase_block_size(void)
{
	return FLASH_ERASE_BYTES;
}


int flash_get_protect_block_size(void)
{
	return FLASH_PROTECT_BYTES;
}


int flash_read(int offset, int size, char *data)
{
	if (size < 0 || offset > usable_flash_size ||
	    offset + size > usable_flash_size)
		return EC_ERROR_UNKNOWN;  /* Invalid range */

	/* Just read the flash from its memory window. */
	/* TODO: is this affected by data cache?  That is, if we read a
	 * block, then alter it, then read it again, do we get the old
	 * data? */
	memcpy(data, (char *)offset, size);
	return EC_SUCCESS;
}


/* Performs a write-buffer operation.  Buffer (FWB) and address (FMA)
 * must be pre-loaded. */
static int WriteBuffer(void)
{
	if (!LM4_FLASH_FWBVAL)
		return EC_SUCCESS;  /* Nothing to do */

	/* Clear previous error status */
	LM4_FLASH_FCMISC = LM4_FLASH_FCRIS;

	/* Start write operation at page boundary */
	LM4_FLASH_FMC2 = 0xa4420001;

	/* Wait for write to complete */
	while (LM4_FLASH_FMC2 & 0x01) {}

	/* Check for error conditions - program failed, erase needed,
	 * voltage error. */
	if (LM4_FLASH_FCRIS & 0x2600)
		return EC_ERROR_UNKNOWN;

	return EC_SUCCESS;
}


int flash_write(int offset, int size, const char *data)
{
	const uint32_t *data32 = (const uint32_t *)data;
	int rv;
	int i;

	if (size < 0 || offset > usable_flash_size ||
	    offset + size > usable_flash_size ||
	    (offset | size) & (FLASH_WRITE_BYTES - 1))
		return EC_ERROR_UNKNOWN;  /* Invalid range */

	/* TODO - safety check - don't allow writing to the image we're
	 * running from */

	/* Get initial page and write buffer index */
	LM4_FLASH_FMA = offset & ~(FLASH_FWB_BYTES - 1);
	i = (offset >> 2) & (FLASH_FWB_WORDS - 1);

	/* Copy words into buffer */
	for ( ; size > 0; size -= 4) {
		LM4_FLASH_FWB[i++] = *data32++;
		if (i == FLASH_FWB_WORDS) {
			rv = WriteBuffer();
			if (rv != EC_SUCCESS)
				return rv;

			/* Advance to next page */
			i = 0;
			LM4_FLASH_FMA += FLASH_FWB_BYTES;
		}
	}

	/* Handle final partial page, if any */
	if (i > 0) {
		rv = WriteBuffer();
		if (rv != EC_SUCCESS)
			return rv;
	}
	return EC_SUCCESS;
}


int flash_erase(int offset, int size)
{
	if (size < 0 || offset > usable_flash_size ||
	    offset + size > usable_flash_size ||
	    (offset | size) & (FLASH_ERASE_BYTES - 1))
		return EC_ERROR_UNKNOWN;  /* Invalid range */

	/* TODO - safety check - don't allow erasing the image we're running
	 * from */

	LM4_FLASH_FCMISC = LM4_FLASH_FCRIS;  /* Clear previous error status */
	LM4_FLASH_FMA = offset;

	for ( ; size > 0; size -= FLASH_ERASE_BYTES) {
		/* Start erase */
		LM4_FLASH_FMC = 0xa4420002;

		/* Wait for erase to complete */
		while (LM4_FLASH_FMC & 0x02) {}

		/* Check for error conditions - erase failed, voltage error */
		if (LM4_FLASH_FCRIS & 0x0a00)
			return EC_ERROR_UNKNOWN;

		LM4_FLASH_FMA += FLASH_ERASE_BYTES;
	}

	return EC_SUCCESS;
}


int flash_get_write_protect_range(int *offset, int *size)
{
	return EC_ERROR_UNIMPLEMENTED;
}


int flash_set_write_protect_range(int offset, int size)
{
	return EC_ERROR_UNIMPLEMENTED;
}


int flash_get_write_protect_status(void)
{
	return EC_ERROR_UNIMPLEMENTED;
}


int flash_init(void)
{
	/* Calculate usable flash size.  Reserve one protection block
	 * at the top to hold the write protect range.  FSIZE already
	 * returns one less than the number of protection pages. */
	usable_flash_size = LM4_FLASH_FSIZE * FLASH_PROTECT_BYTES;

	/* TODO - check WP# GPIO.  If it's set and the flash protect range
	 * is set, write the flash protection registers. */
	return EC_SUCCESS;
}
