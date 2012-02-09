/* Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* EEPROM module for Chrome EC */

#include "eeprom.h"
#include "console.h"
#include "uart.h"
#include "registers.h"
#include "util.h"

/* Size of EEPROM block in bytes */
#define EEPROM_BLOCK_SIZE 64

/* Count of EEPROM blocks */
static int block_count;


/* Waits for the current EEPROM operation to finish. */
static int wait_for_done(void)
{
	/* TODO: how long is a reasonable timeout? */
	int i;
	for (i = 0; i < 1000000; i++) {
		if (!(LM4_EEPROM_EEDONE & 0x01))
			return EC_SUCCESS;
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
		if (LM4_EEPROM_EEDONE)
			return EC_ERROR_UNKNOWN;
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
	uart_printf("EEPROM: %d blocks of %d bytes\n",
		    eeprom_get_block_count(), eeprom_get_block_size());
	uart_printf("  Block-hide flags: 0x%08x\n", LM4_EEPROM_EEHIDE);
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(eeinfo, command_eeprom_info);


static int command_eeprom_read(int argc, char **argv)
{
	int block = 0;
	int offset = 0;
	char *e;
	int rv;
	uint32_t d;

	if (argc < 2) {
		uart_puts("Usage: eeread <block> [offset]\n");
		return EC_ERROR_UNKNOWN;
	}

	block = strtoi(argv[1], &e, 0);
	if (*e) {
		uart_puts("Invalid block\n");
		return EC_ERROR_UNKNOWN;
	}

	if (argc > 2) {
		offset = strtoi(argv[2], &e, 0);
		if (*e) {
			uart_puts("Invalid offset\n");
			return EC_ERROR_UNKNOWN;
		}
	}

	rv = eeprom_read(block, offset, sizeof(d), (char *)&d);
	if (rv == EC_SUCCESS)
		uart_printf("Block %d offset %d = 0x%08x\n",
			    block, offset, d);
	return rv;
}
DECLARE_CONSOLE_COMMAND(eeread, command_eeprom_read);


static int command_eeprom_write(int argc, char **argv)
{
	int block = 0;
	int offset = 0;
	char *e;
	int rv;
	uint32_t d;

	if (argc < 4) {
		uart_puts("Usage: eeread <block> <offset> <data>\n");
		return EC_ERROR_UNKNOWN;
	}

	block = strtoi(argv[1], &e, 0);
	if (*e) {
		uart_puts("Invalid block\n");
		return EC_ERROR_UNKNOWN;
	}
	offset = strtoi(argv[2], &e, 0);
	if (*e) {
		uart_puts("Invalid offset\n");
		return EC_ERROR_UNKNOWN;
	}
	d = strtoi(argv[3], &e, 0);
	if (*e) {
		uart_puts("Invalid data\n");
		return EC_ERROR_UNKNOWN;
	}

	uart_printf("Writing 0x%08x to block %d offset %d...\n",
		    d, block, offset);
	rv = eeprom_write(block, offset, sizeof(d), (char *)&d);
	if (rv == EC_SUCCESS)
		uart_puts("done.\n");
	return rv;
}
DECLARE_CONSOLE_COMMAND(eewrite, command_eeprom_write);


static int command_eeprom_hide(int argc, char **argv)
{
	int block = 0;
	char *e;
	int rv;

	if (argc < 2) {
		uart_puts("Usage: eehide <block>\n");
		return EC_ERROR_UNKNOWN;
	}

	block = strtoi(argv[1], &e, 0);
	if (*e) {
		uart_puts("Invalid block\n");
		return EC_ERROR_UNKNOWN;
	}

	uart_printf("Hiding EEPROM block %d...\n", block);
	rv = eeprom_hide(block);
	if (rv == EC_SUCCESS)
		uart_printf("Done.\n");
	return rv;
}
DECLARE_CONSOLE_COMMAND(eehide, command_eeprom_hide);


/*****************************************************************************/
/* Initialization */


int eeprom_init(void)
{
	volatile uint32_t scratch  __attribute__((unused));

	/* Enable the EEPROM module and delay a few clocks */
	LM4_SYSTEM_RCGCEEPROM = 1;
	scratch = LM4_SYSTEM_RCGCEEPROM;

	wait_for_done();
	block_count = LM4_EEPROM_EESIZE >> 16;

	return EC_SUCCESS;
}
