/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Flash memory module for Chrome EC */

#include "flash.h"
#include "uart.h"
#include "registers.h"
#include "util.h"

#define FLASH_WRITE_BYTES      4
#define FLASH_FWB_WORDS       32
#define FLASH_FWB_BYTES (FLASH_FWB_WORDS * 4)
#define FLASH_ERASE_BYTES   1024
#define FLASH_PROTECT_BYTES 2048

#define BANK_SHIFT 5 /* bank registers have 32bits each, 2^32 */
#define BANK_MASK ((1 << BANK_SHIFT) - 1) /* 5 bits */
#define F_BANK(b) ((b) >> BANK_SHIFT)
#define F_BIT(b) (1 << ((b) & BANK_MASK))


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
	BUILD_ASSERT(FLASH_PROTECT_BYTES == CONFIG_FLASH_BANK_SIZE);
	return FLASH_PROTECT_BYTES;
}


int flash_physical_size(void)
{
	return (LM4_FLASH_FSIZE + 1) * FLASH_PROTECT_BYTES;
}


int flash_physical_read(int offset, int size, char *data)
{
	/* Just read the flash from its memory window. */
	/* TODO: (crosbug.com/p/7473) is this affected by data cache?
	 * That is, if we read a block, then alter it, then read it
	 * again, do we get the old data? */
	memcpy(data, (char *)offset, size);
	return EC_SUCCESS;
}


/* Perform a write-buffer operation.  Buffer (FWB) and address (FMA) must be
 * pre-loaded. */
static int write_buffer(void)
{
	if (!LM4_FLASH_FWBVAL)
		return EC_SUCCESS;  /* Nothing to do */

	/* Clear previous error status */
	LM4_FLASH_FCMISC = LM4_FLASH_FCRIS;

	/* Start write operation at page boundary */
	LM4_FLASH_FMC2 = 0xa4420001;

	/* Wait for write to complete */
	/* TODO: timeout */
	while (LM4_FLASH_FMC2 & 0x01) {}

	/* Check for error conditions - program failed, erase needed,
	 * voltage error. */
	if (LM4_FLASH_FCRIS & 0x2e01)
		return EC_ERROR_UNKNOWN;

	return EC_SUCCESS;
}


int flash_physical_write(int offset, int size, const char *data)
{
	const uint32_t *data32 = (const uint32_t *)data;
	int rv;
	int i;

	/* Get initial write buffer index and page */
	LM4_FLASH_FMA = offset & ~(FLASH_FWB_BYTES - 1);
	i = (offset >> 2) & (FLASH_FWB_WORDS - 1);

	/* Copy words into buffer */
	for ( ; size > 0; size -= 4) {
		LM4_FLASH_FWB[i++] = *data32++;
		if (i == FLASH_FWB_WORDS) {
			rv = write_buffer();
			if (rv != EC_SUCCESS)
				return rv;

			/* Advance to next page */
			i = 0;
			LM4_FLASH_FMA += FLASH_FWB_BYTES;
		}
	}

	/* Handle final partial page, if any */
	if (i > 0) {
		rv = write_buffer();
		if (rv != EC_SUCCESS)
			return rv;
	}
	return EC_SUCCESS;
}


int flash_physical_erase(int offset, int size)
{
	LM4_FLASH_FCMISC = LM4_FLASH_FCRIS;  /* Clear previous error status */
	LM4_FLASH_FMA = offset;

	for ( ; size > 0; size -= FLASH_ERASE_BYTES) {
		/* Start erase */
		LM4_FLASH_FMC = 0xa4420002;

		/* Wait for erase to complete */
		/* TODO: timeout */
		while (LM4_FLASH_FMC & 0x02) {}

		/* Check for error conditions - erase failed, voltage error,
		 * protection error */
		if (LM4_FLASH_FCRIS & 0x0a01)
			return EC_ERROR_UNKNOWN;

		LM4_FLASH_FMA += FLASH_ERASE_BYTES;
	}

	return EC_SUCCESS;
}


int flash_physical_get_protect(int block)
{
	return (LM4_FLASH_FMPPE[F_BANK(block)] & F_BIT(block)) ? 0 : 1;
}


void flash_physical_set_protect(int block)
{
	LM4_FLASH_FMPPE[F_BANK(block)] &= ~F_BIT(block);
}
