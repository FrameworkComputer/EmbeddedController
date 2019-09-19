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
#include "board_id.h"
#include "console.h"
#include "cryptoc/util.h"
#include "extension.h"
#include "flash.h"
#include "flash_log.h"
#include "registers.h"
#include "shared_mem.h"
#include "task.h"
#include "timer.h"
#include "watchdog.h"

#define CPRINTS(format, args...) cprints(CC_EXTENSION, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_EXTENSION, format, ## args)

/* Mutex to prevent concurrent accesses to flash engine. */
static struct mutex flash_mtx;

#ifdef CONFIG_FLASH_LOG
static void flash_log_space_control(int enable)
{
	GWRITE_FIELD(GLOBALSEC, FLASH_REGION5_CTRL, WR_EN, !!enable);
}
#endif

int flash_pre_init(void)
{
	struct g_flash_region regions[4];
	int i, num_regions;

	num_regions = flash_regions_to_enable(regions, ARRAY_SIZE(regions));

	for (i = 0; i < num_regions; i++) {
		int reg_base;

		/* Region range */
		reg_base = GBASE(GLOBALSEC) +
			GOFFSET(GLOBALSEC, FLASH_REGION2_BASE_ADDR) +
			i * 8;

		REG32(reg_base) = regions[i].reg_base;

		/*
		 * The hardware requires a value which is 1 less than the
		 * actual region size.
		 */
		REG32(reg_base + 4) = regions[i].reg_size - 1;

		/* Region permissions. */
		reg_base = GBASE(GLOBALSEC) +
			GOFFSET(GLOBALSEC, FLASH_REGION2_CTRL) +
			i * 4;
		REG32(reg_base) = regions[i].reg_perms;
	}

#ifdef CONFIG_FLASH_LOG
	/*
	 * Allow access to flash elog space and register the access control
	 * function.
	 */
	GREG32(GLOBALSEC, FLASH_REGION5_BASE_ADDR) = CONFIG_FLASH_LOG_BASE;
	GREG32(GLOBALSEC, FLASH_REGION5_SIZE) = CONFIG_FLASH_LOG_SPACE - 1;
	GWRITE_FIELD(GLOBALSEC, FLASH_REGION5_CTRL, EN, 1);
	GWRITE_FIELD(GLOBALSEC, FLASH_REGION5_CTRL, RD_EN, 1);
	flash_log_register_flash_control_callback(flash_log_space_control);
#endif

	/* Create a flash region window for INFO1 access. */
	GREG32(GLOBALSEC, FLASH_REGION7_BASE_ADDR) = FLASH_INFO_MEMORY_BASE;
	GREG32(GLOBALSEC, FLASH_REGION7_SIZE) = FLASH_INFO_SIZE - 1;
	GWRITE_FIELD(GLOBALSEC, FLASH_REGION7_CTRL, EN, 1);
	GWRITE_FIELD(GLOBALSEC, FLASH_REGION7_CTRL, RD_EN, 1);

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

int flash_physical_protect_at_boot(uint32_t new_flags)
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
	OP_READ_BLOCK,
};

static int do_flash_op(enum flash_op op, int is_info_bank,
		       int byte_offset, int words)
{
	volatile uint32_t *fsh_pe_control;
	uint32_t opcode, tmp, errors;
	int retry_count, max_attempts, extra_prog_pulse, i;
	int timedelay_us = 100;
	uint32_t prev_error = 0;

	/* Make sure the smart program/erase algorithms are enabled. */
	if (!GREAD(FLASH, FSH_TIMING_PROG_SMART_ALGO_ON) ||
	    !GREAD(FLASH, FSH_TIMING_ERASE_SMART_ALGO_ON)) {
		CPRINTF("%s:%d\n", __func__, __LINE__);
		return EC_ERROR_UNIMPLEMENTED;
	}

	/* Error status is self-clearing. Read it until it does (we hope). */
	for (i = 0; i < 50; i++) {
		tmp = GREAD(FLASH, FSH_ERROR);
		if (!tmp)
			break;
		usleep(timedelay_us);
	}
	/* If we can't clear the error status register then something is wrong.
	 */
	if (tmp) {
		CPRINTF("%s:%d\n", __func__, __LINE__);
		return EC_ERROR_UNKNOWN;
	}

	/* We have two flash banks. Adjust offset and registers accordingly. */
	if (is_info_bank) {
		/* Only INFO bank operations are supported. */
		fsh_pe_control = GREG32_ADDR(FLASH, FSH_PE_CONTROL1);
	} else if (byte_offset >= CFG_FLASH_HALF) {
		byte_offset -= CFG_FLASH_HALF;
		fsh_pe_control = GREG32_ADDR(FLASH, FSH_PE_CONTROL1);
	} else {
		fsh_pe_control = GREG32_ADDR(FLASH, FSH_PE_CONTROL0);
	}

	/* What are we doing? */
	switch (op) {
	case OP_ERASE_BLOCK:
#ifndef CR50_RELAXED
		if (is_info_bank)
			/* Erasing the INFO bank from the RW section is
			 * unsupported. */
			return EC_ERROR_INVAL;
#endif
		opcode = 0x31415927;
		words = 0;			/* don't care, really */
		/* This number is based on the TSMC spec Nme=Terase/Tsme */
		max_attempts = 45;
		break;
	case OP_WRITE_BLOCK:
		opcode = 0x27182818;
		words--;		     /* count register is zero-based */
		/* This number is based on the TSMC spec Nmp=Tprog/Tsmp */
		max_attempts = 9;
		break;
	case OP_READ_BLOCK:
		if (!is_info_bank)
			/* This code path only supports reading from
			 * the INFO bank.
			 */
			return EC_ERROR_INVAL;
		opcode = 0x16021765;
		words = 1;
		max_attempts = 9;
		break;
	default:
		return EC_ERROR_INVAL;
	}

	/*
	 * Set the parameters. For writes, we assume the write buffer is
	 * already filled before we call this function.
	 */
	GWRITE_FIELD(FLASH, FSH_TRANS, OFFSET,
		     byte_offset / 4);		  /* word offset */
	GWRITE_FIELD(FLASH, FSH_TRANS, MAINB, is_info_bank ? 1 : 0);
	GWRITE_FIELD(FLASH, FSH_TRANS, SIZE, words);

	/* TODO: Make sure this function isn't getting called "too often" in
	 * between erases.
	 */
	extra_prog_pulse = 0;
	for (retry_count = 0; retry_count < max_attempts; retry_count++) {
		/* Kick it off */
		GWRITE(FLASH, FSH_PE_EN, 0xb11924e1);
		*fsh_pe_control = opcode;

		/* Wait for completion. 150ms should be enough
		 * (crosbug.com/p/45366).
		 */
		for (i = 0; i < 1500; i++) {
			tmp = *fsh_pe_control;
			if (!tmp)
				break;
			usleep(timedelay_us);
		}

		/* Timed out waiting for control register to clear */
		if (tmp) {
			/* Stop the failed operation. */
			*fsh_pe_control = 0;
			CPRINTF("%s:%d\n", __func__, __LINE__);
			return EC_ERROR_UNKNOWN;
		}
		/* Check error status */
		errors = GREAD(FLASH, FSH_ERROR);

		if (errors && (errors != prev_error)) {
			prev_error = errors;
			CPRINTF("%s:%d errors %x fsh_pe_control %p\n",
				__func__, __LINE__, errors, fsh_pe_control);
		}
		/* Error status is self-clearing. Read it until it does
		 * (we hope).
		 */
		for (i = 0; i < 50; i++) {
			tmp = GREAD(FLASH, FSH_ERROR);
			if (!tmp)
				break;
			usleep(timedelay_us);
		}
		/* If we can't clear the error status register then something
		 * is wrong.
		 */
		if (tmp) {
			CPRINTF("%s:%d\n", __func__, __LINE__);
			return EC_ERROR_UNKNOWN;
		}
		/* The operation was successful. */
		if (!errors) {
			/* From the spec:
			 * "In addition, one more program pulse is needed after
			 * program verification is passed."
			 */
			if (op == OP_WRITE_BLOCK && !extra_prog_pulse) {
				extra_prog_pulse = 1;
				max_attempts++;
				continue;
			}
			return EC_SUCCESS;
		}
		/* If there were errors after completion retry. */
		watchdog_reload();
	}
	CPRINTF("%s:%d, retry count %d\n", __func__, __LINE__, retry_count);
	return EC_ERROR_UNKNOWN;
}

/* Write up to CONFIG_FLASH_WRITE_IDEAL_SIZE bytes at once */
static int write_batch(int byte_offset, int is_info_bank,
		       int words, const uint8_t *data)
{
	volatile uint32_t *fsh_wr_data = GREG32_ADDR(FLASH, FSH_WR_DATA0);
	uint32_t val;
	int i;
	int rv;

	mutex_lock(&flash_mtx);

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

	rv = do_flash_op(OP_WRITE_BLOCK, is_info_bank, byte_offset, words);

	mutex_unlock(&flash_mtx);

	return rv;
}

static int flash_physical_write_internal(int byte_offset, int is_info_bank,
				int num_bytes, const char *data)
{
	int num, ret;

	/* The offset and size must be a multiple of CONFIG_FLASH_WRITE_SIZE */
	if (byte_offset % CONFIG_FLASH_WRITE_SIZE ||
	    num_bytes % CONFIG_FLASH_WRITE_SIZE)
		return EC_ERROR_INVAL;

	while (num_bytes) {
		num = MIN(num_bytes, CONFIG_FLASH_WRITE_IDEAL_SIZE);
		/*
		 * Make sure that the write operation will not go
		 * past a CONFIG_FLASH_ROW_SIZE boundary.
		 */
		num = MIN(num, CONFIG_FLASH_ROW_SIZE -
			  byte_offset % CONFIG_FLASH_ROW_SIZE);
		ret = write_batch(byte_offset,
				  is_info_bank,
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

int flash_physical_write(int byte_offset, int num_bytes, const char *data)
{
	return flash_physical_write_internal(byte_offset, 0, num_bytes, data);
}

int flash_physical_info_read_word(int byte_offset, uint32_t *dst)
{
	int ret;

	if (byte_offset % CONFIG_FLASH_WRITE_SIZE)
		return EC_ERROR_INVAL;

	mutex_lock(&flash_mtx);

	ret = do_flash_op(OP_READ_BLOCK, 1, byte_offset, 1);
	if (ret == EC_SUCCESS)
		*dst = GREG32(FLASH, FSH_DOUT_VAL1);

	mutex_unlock(&flash_mtx);

	return ret;
}

void flash_info_write_enable(void)
{
	GWRITE_FIELD(GLOBALSEC, FLASH_REGION7_CTRL, WR_EN, 1);
}

void flash_info_write_disable(void)
{
	GWRITE_FIELD(GLOBALSEC, FLASH_REGION7_CTRL, WR_EN, 0);
}

int flash_info_physical_write(int byte_offset, int num_bytes, const char *data)
{
	if (byte_offset < 0 || num_bytes < 0 ||
		byte_offset + num_bytes > FLASH_INFO_SIZE ||
		(byte_offset | num_bytes) & (CONFIG_FLASH_WRITE_SIZE - 1))
		return EC_ERROR_INVAL;

	return flash_physical_write_internal(byte_offset, 1, num_bytes, data);
}

int flash_physical_erase(int byte_offset, int num_bytes)
{
	int ret;

	/* Offset and size must be a multiple of CONFIG_FLASH_ERASE_SIZE */
	if (byte_offset % CONFIG_FLASH_ERASE_SIZE ||
	    num_bytes % CONFIG_FLASH_ERASE_SIZE)
		return EC_ERROR_INVAL;

	while (num_bytes) {

		mutex_lock(&flash_mtx);

		/* We may be asked to erase multiple banks */
		ret = do_flash_op(OP_ERASE_BLOCK,
				  0,              /* not the INFO bank */
				  byte_offset,
				  num_bytes / 4); /* word count */

		mutex_unlock(&flash_mtx);

		if (ret) {
			CPRINTF("Failed to erase block at %x\n", byte_offset);
			return ret;
		}

		num_bytes -= CONFIG_FLASH_ERASE_SIZE;
		byte_offset += CONFIG_FLASH_ERASE_SIZE;
	}

	return EC_SUCCESS;
}


/* Enable write access to the backup RO section. */
void flash_open_ro_window(uint32_t offset, size_t size_b)
{
	GREG32(GLOBALSEC, FLASH_REGION6_BASE_ADDR) =
		offset + CONFIG_PROGRAM_MEMORY_BASE;
	GREG32(GLOBALSEC, FLASH_REGION6_SIZE) = size_b - 1;
	GWRITE_FIELD(GLOBALSEC, FLASH_REGION6_CTRL, EN, 1);
	GWRITE_FIELD(GLOBALSEC, FLASH_REGION6_CTRL, RD_EN, 1);
	GWRITE_FIELD(GLOBALSEC, FLASH_REGION6_CTRL, WR_EN, 1);
}

#ifdef CR50_DEV
/*
 * The seed is the first 32 bytes of the manufacture state space. That is all
 * we care about. We can ignore the rest of the manufacture state.
 */
#define ENDORSEMENT_SEED_SIZE 32

static enum vendor_cmd_rc vc_endorsement_seed(enum vendor_cmd_cc code,
					      void *buf,
					      size_t input_size,
					      size_t *response_size)
{
	uint8_t endorsement_seed[ENDORSEMENT_SEED_SIZE];
	int rv = VENDOR_RC_SUCCESS;
	int is_erased = 1;
	int set_seed = input_size == ENDORSEMENT_SEED_SIZE;
	int i;
	uint32_t *p;
	int offset;

	*response_size = 0;
	if (input_size && !set_seed) {
		CPRINTS("%s: invalid seed", __func__);
		return VENDOR_RC_BOGUS_ARGS;
	}

	/* Read the endorsement key seed. */
	p = (uint32_t *)endorsement_seed;
	for (i = 0; i < (ENDORSEMENT_SEED_SIZE / sizeof(*p)); i++) {
		offset = FLASH_INFO_MANUFACTURE_STATE_OFFSET + i * sizeof(*p);
		if (flash_physical_info_read_word(offset, p + i) !=
		    EC_SUCCESS) {
			CPRINTS("%s: failed read", __func__);
			return VENDOR_RC_INTERNAL_ERROR;
		}
		if (p[i] != 0xffffffff)
			is_erased = 0;
	}

	if (set_seed && !is_erased)  {
		CPRINTS("%s: seed already set!", __func__);
		return VENDOR_RC_NOT_ALLOWED;
	}

	if (!input_size) {
		*response_size = ENDORSEMENT_SEED_SIZE;
		memcpy(buf, endorsement_seed, *response_size);
		return VENDOR_RC_SUCCESS;
	}

	flash_info_write_enable();
	if (flash_info_physical_write(FLASH_INFO_MANUFACTURE_STATE_OFFSET,
				      input_size,
				      (char *)buf) != EC_SUCCESS) {
		CPRINTS("%s: failed write", __func__);
		rv = VENDOR_RC_INTERNAL_ERROR;
	}
	flash_info_write_disable();
	return rv;
}
DECLARE_VENDOR_COMMAND(VENDOR_CC_ENDORSEMENT_SEED, vc_endorsement_seed);
#endif
#ifdef CR50_RELAXED
static int command_erase_flash_info(int argc, char **argv)
{
	int i;
	int rv;
	struct info1_layout *info1;
	uint32_t *p;

	rv = shared_mem_acquire(sizeof(*info1), (char **)&info1);
	if (rv != EC_SUCCESS) {
		ccprintf("Failed to allocate memory for info1!\n");
		return rv;
	}

	/* Read the entire info1. */
	p = (uint32_t *)info1;
	for (i = 0; i < (sizeof(*info1) / sizeof(*p)); i++) {
		if (flash_physical_info_read_word(i * sizeof(*p), p + i) !=
		    EC_SUCCESS) {
			ccprintf("Failed to read word %d!\n", i);
			goto exit;
		}
	}

#ifdef CR50_SQA
	/*
	 * SQA images erase INFO1 RW mask, but do not allow erasing board ID.
	 *
	 * If compiled with CR50_SQA=1, board ID flags will set to zero, if
	 * compiled with CR50_SQA=2 or greater, board ID flags can be set to
	 * an arbitrary value passed in on the command line, but guaranteeing
	 * not to lock out the currently running image.
	 */
	{
		uint32_t flags = 0;
#if CR50_SQA > 1
		if (argc > 1) {
			char *e;

			flags = strtoi(argv[1], &e, 0);
			if (*e) {
				rv = EC_ERROR_PARAM1;
				goto exit;
			}
		}
#endif
		if (board_id_is_blank(&info1->board_space.bid)) {
			ccprintf("BID is erased. Not modifying flags\n");
		} else {
			ccprintf("setting BID flags to %x\n", flags);
			info1->board_space.bid.flags = flags;
		}
		if (check_board_id_vs_header(&info1->board_space.bid,
					     get_current_image_header())) {
			ccprintf("Flags %x would lock out current image\n",
				 flags);
			rv = EC_ERROR_PARAM1;
			goto exit;
		}
	}
#else  /* CR50_SQA   ^^^^^^ defined    vvvvvvv Not defined. */
	/*
	 * This must be CR50_DEV=1 image, just erase the board information
	 * space.
	 */
	memset(&info1->board_space, 0xff, sizeof(info1->board_space));
#endif /* CR50_SQA Not defined. */

	memset(info1->rw_info_map, 0xff, sizeof(info1->rw_info_map));

	mutex_lock(&flash_mtx);

	flash_info_write_enable();

	rv = do_flash_op(OP_ERASE_BLOCK, 1, 0, 512);

	mutex_unlock(&flash_mtx);

	if (rv != EC_SUCCESS) {
		ccprintf("Failed to erase info space!\n");
		goto exit;
	}

	rv = flash_info_physical_write(0, sizeof(*info1), (char *)info1);
	if (rv != EC_SUCCESS)
		ccprintf("Failed write back info1 contents!\n");

 exit:
	flash_info_write_disable();
	always_memset(info1, 0, sizeof(*info1));
	shared_mem_release(info1);
	return rv;
}
DECLARE_SAFE_CONSOLE_COMMAND(eraseflashinfo, command_erase_flash_info,
#if defined(CR50_SQA) && (CR50_SQA > 1)
			     "[bid flags]",
			     "Erase INFO1 flash space and set Board ID flags");
#else
			     "", "Erase INFO1 flash space");
#endif
#endif
