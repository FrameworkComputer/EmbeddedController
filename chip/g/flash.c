/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * The SoC's internal flash consists of two separate "banks" of 256K bytes each
 * (sometimes called "macros" because of how they're implemented in Verilog).
 *
 * Each flash bank contains 128 "blocks" or "pages" of 2K bytes each. These
 * blocks can be erased individually, or the entire bank can be erased at once.
 *
 * When the flash content is erased, all its bits are set to 1.
 *
 * The flash content can be read directly as bytes, halfwords, or words, just
 * like any memory region. However, writes can only happen through special
 * operations, in units of properly aligned 32-bit words.
 *
 * The flash controller has a 32-word write buffer. This allows up to 32
 * adjacent words (128 bytes) within a bank to be written in one operation.
 *
 * Multiple writes to the same flash word can be done without first erasing the
 * block, however:
 *
 * A) writes can only change stored bits from 1 to 0, and
 *
 * B) the manufacturer recommends that no more than two writes be done between
 *    erase cycles for best results (in terms of reliability, longevity, etc.)
 *
 * All of this is fairly typical of most flash parts. This next thing is NOT
 * typical:
 *
 * +--------------------------------------------------------------------------+
 * + While any write or erase operation is in progress, ALL other access to   +
 * + that entire bank is stalled. Data reads, instruction fetches, interrupt  +
 * + vector lookup -- every access blocks until the flash operation finishes. +
 * +--------------------------------------------------------------------------+
 *
 */

#include "common.h"
#include "flash.h"
#include "registers.h"
#include "timer.h"

int flash_pre_init(void)
{
	/* Enable access to the upper half of the flash */
	uint32_t half = CONFIG_FLASH_SIZE / 2;

	GWRITE(GLOBALSEC, FLASH_REGION2_BASE_ADDR,
	       CONFIG_MAPPED_STORAGE_BASE + half);
	GWRITE(GLOBALSEC, FLASH_REGION2_SIZE, half - 1);
	GWRITE_FIELD(GLOBALSEC, FLASH_REGION2_CTRL, WR_EN, 1);
	GWRITE_FIELD(GLOBALSEC, FLASH_REGION2_CTRL, RD_EN, 1);
	GWRITE_FIELD(GLOBALSEC, FLASH_REGION2_CTRL, EN, 1);

	return EC_SUCCESS;
}

int flash_physical_get_protect(int bank)
{
	return 0;				/* Not protected */
}

uint32_t flash_physical_get_protect_flags(void)
{
	return 0;				/* no flags set */
}

uint32_t flash_physical_get_valid_flags(void)
{
	/* These are the flags we're going to pay attention to */
	return EC_FLASH_PROTECT_RO_AT_BOOT |
		EC_FLASH_PROTECT_RO_NOW |
		EC_FLASH_PROTECT_ALL_NOW;
}

uint32_t flash_physical_get_writable_flags(uint32_t cur_flags)
{
	return 0;				/* no flags writable */
}

int flash_physical_protect_at_boot(enum flash_wp_range range)
{
	return EC_SUCCESS;			/* yeah, I did it. */
}

int flash_physical_protect_now(int all)
{
	return EC_SUCCESS;			/* yeah, I did it. */
}


enum flash_op {
	OP_ERASE_BLOCK,
	OP_WRITE_BLOCK,
};

static int do_flash_op(enum flash_op op, int byte_offset, int words)
{
	volatile uint32_t *fsh_pe_control;
	uint32_t opcode, tmp, errors;
	int i;
	int timedelay = 100;	     /* TODO(crosbug.com/p/45366): how long? */

	/* Error status is self-clearing. Read it until it does (we hope). */
	for (i = 0; i < 50; i++) {
		tmp = GREAD(FLASH, FSH_ERROR);
		if (!tmp)
			break;
		usleep(timedelay);
	}
	/* TODO: Is it even possible that we can't clear the error status?
	 * What should/can we do about that? */

	/* We have two flash banks. Adjust offset and registers accordingly. */
	if (byte_offset >= CONFIG_FLASH_SIZE / 2) {
		byte_offset -= CONFIG_FLASH_SIZE / 2;
		fsh_pe_control = GREG32_ADDR(FLASH, FSH_PE_CONTROL1);
	} else {
		fsh_pe_control = GREG32_ADDR(FLASH, FSH_PE_CONTROL0);
	}

	/* What are we doing? */
	switch (op) {
	case OP_ERASE_BLOCK:
		opcode = 0x31415927;
		words = 0;			/* don't care, really */
		break;
	case OP_WRITE_BLOCK:
		opcode = 0x27182818;
		words--;		     /* count register is zero-based */
		break;
	}

	/*
	 * Set the parameters. For writes, we assume the write buffer is
	 * already filled before we call this function.
	 */
	GWRITE_FIELD(FLASH, FSH_TRANS, OFFSET,
		     byte_offset / 4);		  /* word offset */
	GWRITE_FIELD(FLASH, FSH_TRANS, MAINB, 0); /* NOT the info bank */
	GWRITE_FIELD(FLASH, FSH_TRANS, SIZE, words);

	/* Kick it off */
	GWRITE(FLASH, FSH_PE_EN, 0xb11924e1);
	*fsh_pe_control = opcode;

	/* Wait for completion */
	for (i = 0; i < 50; i++) {
		tmp = *fsh_pe_control;
		if (!tmp)
			break;
		usleep(timedelay);
	}

	/* Timed out waiting for control register to clear */
	if (tmp)
		return EC_ERROR_UNKNOWN;

	/* Check error status */
	errors = GREAD(FLASH, FSH_ERROR);

	/* Error status is self-clearing. Read it until it does (we hope). */
	for (i = 0; i < 50; i++) {
		tmp = GREAD(FLASH, FSH_ERROR);
		if (!tmp)
			break;
		usleep(timedelay);
	}

	/* If there were errors after completion, or if we can't clear the
	 * error status register (is that likely?) then something is wrong. */
	if (errors || tmp)
		return EC_ERROR_UNKNOWN;

	/* The operation was successful. */
	/* TODO: Should we read it back to be sure? */
	return EC_SUCCESS;
}

/* Write up to CONFIG_FLASH_WRITE_IDEAL_SIZE bytes at once */
static int write_batch(int byte_offset, int words, const uint8_t *data)
{
	volatile uint32_t *fsh_wr_data = GREG32_ADDR(FLASH, FSH_WR_DATA0);
	uint32_t val;
	int i;

	/* Load the write buffer. */
	for (i = 0; i < words; i++) {
		/*
		 * We have to write 32-bit values, but we can't guarantee
		 * alignment for the data. We'll just assemble the word
		 * manually to avoid alignment faults. Note that we're assuming
		 * little-endian order here.
		 */
		val = ((data[3] << 24) | (data[2] << 16) |
		       (data[1] << 8) | data[0]);

		*fsh_wr_data = val;
		data += 4;
		fsh_wr_data++;
	}

	return do_flash_op(OP_WRITE_BLOCK, byte_offset, words);
}

int flash_physical_write(int byte_offset, int num_bytes, const char *data)
{
	int num, ret;

	/* The offset and size must be a multiple of CONFIG_FLASH_WRITE_SIZE */
	if (byte_offset % CONFIG_FLASH_WRITE_SIZE ||
	    num_bytes % CONFIG_FLASH_WRITE_SIZE)
		return EC_ERROR_INVAL;

	while (num_bytes) {
		num = MIN(num_bytes, CONFIG_FLASH_WRITE_IDEAL_SIZE);
		ret = write_batch(byte_offset,
				  num / 4,	/* word count */
				  (const uint8_t *)data);
		if (ret)
			return ret;

		num_bytes -= num;
		byte_offset += num;
		data += num;
	}

	return EC_SUCCESS;
}

int flash_physical_erase(int byte_offset, int num_bytes)
{
	int ret;

	/* Offset and size must be a multiple of CONFIG_FLASH_ERASE_SIZE */
	if (byte_offset % CONFIG_FLASH_ERASE_SIZE ||
	    num_bytes % CONFIG_FLASH_ERASE_SIZE)
		return EC_ERROR_INVAL;

	while (num_bytes) {
		/* We may be asked to erase multiple banks */
		ret = do_flash_op(OP_ERASE_BLOCK,
				  byte_offset,
				  num_bytes / 4); /* word count */
		if (ret)
			return ret;

		num_bytes -= CONFIG_FLASH_ERASE_SIZE;
		byte_offset += CONFIG_FLASH_ERASE_SIZE;
	}

	return EC_SUCCESS;
}
