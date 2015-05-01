/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* System module for Chrome EC */

#include "console.h"
#include "timer.h"
#include "util.h"
#include "watchdog.h"

static int command_mem_dump(int argc, char **argv)
{
	volatile uint32_t *address;
	uint32_t value, num = 1, i;
	char *e;

	if (argc < 2)
		return EC_ERROR_PARAM_COUNT;

	address = (uint32_t *)(uintptr_t)strtoi(argv[1], &e, 0);
	if (*e)
		return EC_ERROR_PARAM1;

	if (argc >= 3)
		num = strtoi(argv[2], &e, 0);

	for (i = 0; i < num; i++) {
		value = address[i];
		if (0 == (i%4))
			ccprintf("\n%08X: %08x", address+i, value);
		else
			ccprintf(" %08x", value);
		cflush();

		/* Lots of output could take a while.
		 * Let other things happen, too */
		if (!(i % 0x100)) {
			watchdog_reload();
			usleep(10 * MSEC);
		}
	}
	ccprintf("\n");
	cflush();
	return EC_SUCCESS;
}

DECLARE_CONSOLE_COMMAND(md, command_mem_dump,
			"addr [num]",
			"dump num of words (4B) in memory",
			NULL);

static int command_read_word(int argc, char **argv)
{
	volatile uint32_t *address;
	uint32_t value;
	unsigned access_size = 4;
	unsigned argc_offs = 0;
	char *e;

	if (argc < 2)
		return EC_ERROR_PARAM_COUNT;

	if (argc > 2) {
		if ((argv[1][0] == '.') && (strlen(argv[1]) == 2)) {
			argc_offs = 1;
			switch (argv[1][1]) {
			case 'b':
				access_size = 1;
				break;
			case 's':
				access_size = 2;
				break;
			default:
				return EC_ERROR_PARAM1;
			}
		}
	}

	address = (uint32_t *)(uintptr_t)strtoi(argv[1 + argc_offs], &e, 0);
	if (*e)
		return EC_ERROR_PARAM1 + argc_offs;

	/* Just reading? */
	if ((argc - argc_offs) < 3) {
		switch (access_size) {
		case 1:
			ccprintf("read 0x%p = 0x%02x\n",
				 address, *((uint8_t *)address));
			break;
		case 2:
			ccprintf("read 0x%p = 0x%04x\n",
				 address, *((uint16_t *)address));
			break;

		default:
			ccprintf("read 0x%p = 0x%08x\n",  address, *address);
			break;
		}
		return EC_SUCCESS;
	}

	/* Writing! */
	value = strtoi(argv[2 + argc_offs], &e, 0);
	if (*e)
		return EC_ERROR_PARAM2 + argc_offs;

	switch (access_size) {
	case 1:
		ccprintf("write 0x%p = 0x%02x\n", address, (uint8_t)value);
		cflush();  /* Flush before writing in case this crashes */
		*((uint8_t *)address) = (uint8_t)value;
		break;
	case 2:
		ccprintf("write 0x%p = 0x%04x\n", address, (uint16_t)value);
		cflush();
		*((uint16_t *)address) = (uint16_t)value;
		break;
	default:
		ccprintf("write 0x%p = 0x%02x\n", address, value);
		cflush();
		*address = value;
		break;
	}

	return EC_SUCCESS;
}

DECLARE_CONSOLE_COMMAND
	(rw, command_read_word,
	 "addr [.{b|s}] [value]",
	 "Read or write a word in memory optionally specifying the size",
	 NULL);
