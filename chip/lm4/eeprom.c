/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* EEPROM module for Chrome EC */

#include "clock.h"
#include "console.h"
#include "eeprom.h"
#include "registers.h"
#include "timer.h"
#include "util.h"
#include "watchdog.h"

/* Size of EEPROM block in bytes */
#define EEPROM_BLOCK_SIZE 64

/* Count of EEPROM blocks */
static int block_count;

/*
 * Wait for the current EEPROM operation to finish; all operations but write
 * should normally finish in 4 system clocks, but worst case is up to
 * 1800ms if the EEPROM needs to do an internal page erase/copy.  We must
 * spin-wait for this delay, because EEPROM operations will fail if the chip
 * drops to sleep mode.
 */
static int wait_for_done(void)
{
	int j;

	for (j = 0; j < 20; j++) {  /* 20 * 100 ms = 2000 ms */
		uint64_t tstop = get_time().val + 100 * MSEC;
		while (get_time().val < tstop) {
			if (!(LM4_EEPROM_EEDONE & 0x01))
				return EC_SUCCESS;
		}
		watchdog_reload();
	}

	return EC_ERROR_UNKNOWN;
}


int eeprom_get_block_count(void)
{
	return block_count;
}


int eeprom_get_block_size(void)
{
	return EEPROM_BLOCK_SIZE;
}


int eeprom_read(int block, int offset, int size, char *data)
{
	uint32_t *d = (uint32_t *)data;
	int rv;

	if (block < 0 || block >= block_count ||
	    offset < 0 || offset > EEPROM_BLOCK_SIZE || offset & 3 ||
	    size < 0 || offset + size >= EEPROM_BLOCK_SIZE || size & 3)
		return EC_ERROR_UNKNOWN;

	rv = wait_for_done();
	if (rv)
		return rv;

	LM4_EEPROM_EEBLOCK = block;
	if (LM4_EEPROM_EEBLOCK != block)
		return EC_ERROR_UNKNOWN;  /* Error setting block */

	LM4_EEPROM_EEOFFSET = offset >> 2;

	for ( ; size; size -= sizeof(uint32_t))
		*(d++) = LM4_EEPROM_EERDWRINC;

	return EC_SUCCESS;
}


int eeprom_write(int block, int offset, int size, const char *data)
{
	uint32_t *d = (uint32_t *)data;
	int rv;

	if (block < 0 || block >= block_count ||
	    offset < 0 || offset > EEPROM_BLOCK_SIZE || offset & 3 ||
	    size < 0 || offset + size >= EEPROM_BLOCK_SIZE || size & 3)
		return EC_ERROR_UNKNOWN;

	rv = wait_for_done();
	if (rv)
		return rv;

	LM4_EEPROM_EEBLOCK = block;
	if (LM4_EEPROM_EEBLOCK != block)
		return EC_ERROR_UNKNOWN;  /* Error setting block */

	LM4_EEPROM_EEOFFSET = offset >> 2;

	/* Write 32 bits at a time; wait for each write to complete */
	for ( ; size; size -= sizeof(uint32_t)) {
		LM4_EEPROM_EERDWRINC = *(d++);

		rv = wait_for_done();
		if (rv)
			return rv;

		if (LM4_EEPROM_EEDONE & 0x10) {
			/* Failed due to write protect */
			return EC_ERROR_ACCESS_DENIED;
		} else if (LM4_EEPROM_EEDONE & 0x100) {
			/* Failed due to program voltage level */
			return EC_ERROR_UNKNOWN;
		}
	}

	return EC_SUCCESS;
}


int eeprom_hide(int block)
{
	/* Block 0 can't be hidden */
	if (block <= 0 || block >= block_count)
		return EC_ERROR_UNKNOWN;

	LM4_EEPROM_EEHIDE |= 1 << block;
	return EC_SUCCESS;
}


/*****************************************************************************/
/* Console commands */

static int command_eeprom_info(int argc, char **argv)
{
	ccprintf("%d blocks @ %d bytes, hide=0x%08x\n",
		 eeprom_get_block_count(), eeprom_get_block_size(),
		 LM4_EEPROM_EEHIDE);
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(eeinfo, command_eeprom_info,
			NULL,
			"Print EEPROM info",
			NULL);


static int command_eeprom_read(int argc, char **argv)
{
	int block = 0;
	int offset = 0;
	char *e;
	int rv;
	uint32_t d;

	if (argc < 2)
		return EC_ERROR_PARAM_COUNT;

	block = strtoi(argv[1], &e, 0);
	if (*e)
		return EC_ERROR_PARAM1;

	if (argc > 2) {
		offset = strtoi(argv[2], &e, 0);
		if (*e)
			return EC_ERROR_PARAM2;
	}

	rv = eeprom_read(block, offset, sizeof(d), (char *)&d);
	if (rv == EC_SUCCESS)
		ccprintf("%d:%d = 0x%08x\n", block, offset, d);
	return rv;
}
DECLARE_CONSOLE_COMMAND(eeread, command_eeprom_read,
			"block [offset]",
			"Read a word of EEPROM",
			NULL);


static int command_eeprom_write(int argc, char **argv)
{
	int block = 0;
	int offset = 0;
	char *e;
	uint32_t d;

	if (argc < 4)
		return EC_ERROR_PARAM_COUNT;

	block = strtoi(argv[1], &e, 0);
	if (*e)
		return EC_ERROR_PARAM1;
	offset = strtoi(argv[2], &e, 0);
	if (*e)
		return EC_ERROR_PARAM2;
	d = strtoi(argv[3], &e, 0);
	if (*e)
		return EC_ERROR_PARAM3;

	ccprintf("Writing 0x%08x to %d:%d...\n", d, block, offset);
	return eeprom_write(block, offset, sizeof(d), (char *)&d);
}
DECLARE_CONSOLE_COMMAND(eewrite, command_eeprom_write,
			"block offset value",
			"Write a word of EEPROM",
			NULL);


#ifdef CONSOLE_COMMAND_EEHIDE
static int command_eeprom_hide(int argc, char **argv)
{
	int block = 0;
	char *e;

	if (argc < 2)
		return EC_ERROR_PARAM_COUNT;

	block = strtoi(argv[1], &e, 0);
	if (*e)
		return EC_ERROR_PARAM1;

	ccprintf("Hiding block %d\n", block);
	return eeprom_hide(block);
}
DECLARE_CONSOLE_COMMAND(eehide, command_eeprom_hide,
			"block",
			"Hide a block of EEPROM",
			NULL);
#endif


/*****************************************************************************/
/* Initialization */


int eeprom_init(void)
{
	/* Enable the EEPROM module and delay a few clocks */
	LM4_SYSTEM_RCGCEEPROM = 1;
	clock_wait_cycles(6);

	/* Wait for internal EEPROM init to finish */
	wait_for_done();

	/* Store block count */
	block_count = LM4_EEPROM_EESIZE >> 16;

	/*
	 * Handle resetting the EEPROM module to clear state from a previous
	 * error condition.
	 */
	if (LM4_EEPROM_EESUPP & 0xc0) {
		LM4_SYSTEM_SREEPROM = 1;
		clock_wait_cycles(200);
		LM4_SYSTEM_SREEPROM = 0;

		/* Wait again for internal init to finish */
		clock_wait_cycles(6);
		wait_for_done();

		/* Fail if error condition didn't clear */
		if (LM4_EEPROM_EESUPP & 0xc0)
			return EC_ERROR_UNKNOWN;
	}

	return EC_SUCCESS;
}
