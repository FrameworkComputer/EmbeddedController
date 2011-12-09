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

#define BANK_SHIFT 5 /* bank registers have 32bits each, 2^32 */
#define BANK_MASK ((1 << BANK_SHIFT) - 1) /* 5 bits */
#define F_BANK(b) ((b) >> BANK_SHIFT)
#define F_BIT(b) (1 << ((b) & BANK_MASK))

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

/* Get write protect status of single flash block
 * return value:
 *   0 - WP
 *   non-zero - writable
 */
static uint32_t get_block_wp(int block)
{
	return LM4_FLASH_FMPPE[F_BANK(block)] & F_BIT(block);
}

static void set_block_wp(int block)
{
	LM4_FLASH_FMPPE[F_BANK(block)] &= ~F_BIT(block);
}

static int find_first_wp_block(void)
{
	int block;
	for (block = 0; block < LM4_FLASH_FSIZE; block++)
		if (get_block_wp(block) == 0)
			return block;
	return -1;
}

static int find_last_wp_block(void)
{
	int block;
	for (block = LM4_FLASH_FSIZE - 1; block >= 0; block--)
		if (get_block_wp(block) == 0)
			return block;
	return -1;
}

static int get_wp_range(int *start, int *nblock)
{
	int start_blk, end_blk;

	start_blk = find_first_wp_block();

	if (start_blk < 0) {
		/* Flash is not write protected */
		*start = 0;
		*nblock = 0;
		return EC_SUCCESS;
	}

	/* TODO: Sanity check the shadow value? */

	end_blk = find_last_wp_block();
	*nblock = end_blk - start_blk + 1;
	*start = start_blk;
	return EC_SUCCESS;
}


static int set_wp_range(int start, int nblock)
{
	int end_blk, block;

	if (nblock == 0)
		return EC_SUCCESS;

	end_blk = (start + nblock - 1);

	for (block = start; block <= end_blk; block++)
		set_block_wp(block);

	return EC_SUCCESS;
}

int flash_get_write_protect_range(int *offset, int *size)
{
	int start, nblock;
	int rv;

	rv = get_wp_range(&start, &nblock);
	if (rv)
		return rv;

	*size = nblock * FLASH_PROTECT_BYTES;
	*offset = start * FLASH_PROTECT_BYTES;
	return EC_SUCCESS;
}

int flash_set_write_protect_range(int offset, int size)
{
	int start, nblock;
	int rv;

	if ((offset < 0) || (size < 0) || ((offset + size) >
			(LM4_FLASH_FSIZE * FLASH_PROTECT_BYTES)))
		return EC_ERROR_UNKNOWN; /* Invalid range */

	rv = flash_get_write_protect_status();

	if (rv & EC_FLASH_WP_RANGE_LOCKED) {
		if (size == 0) {
			/* TODO: Clear shadow if system WP is asserted */
			/* TODO: Reboot EC */
			return EC_SUCCESS;
		}

		return EC_ERROR_UNKNOWN; /* Range locked */
	}

	start = offset / FLASH_PROTECT_BYTES;
	nblock = ((offset + size - 1) / FLASH_PROTECT_BYTES) - start + 1;
	rv = set_wp_range(start, nblock);
	if (rv)
		return rv;

	return EC_SUCCESS;
}


int flash_get_write_protect_status(void)
{
	int start, nblock;
	int rv;

	rv = get_wp_range(&start, &nblock);
	if (rv)
		return rv;

	rv = 0;
	if (nblock)
		rv |= EC_FLASH_WP_RANGE_LOCKED;
	/* TODO: get WP gpio*/

	return rv;
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
