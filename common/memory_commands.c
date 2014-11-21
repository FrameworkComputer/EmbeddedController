/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* System module for Chrome EC */

#include "console.h"
#include "util.h"

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
	char *e;

	if (argc < 2)
		return EC_ERROR_PARAM_COUNT;

	address = (uint32_t *)(uintptr_t)strtoi(argv[1], &e, 0);
	if (*e)
		return EC_ERROR_PARAM1;

	/* Just reading? */
	if (argc < 3) {
		value = *address;
		ccprintf("read 0x%p = 0x%08x\n", address, value);
		return EC_SUCCESS;
	}

	/* Writing! */
	value = strtoi(argv[2], &e, 0);
	if (*e)
		return EC_ERROR_PARAM2;

	ccprintf("write 0x%p = 0x%08x\n", address, value);
	cflush();  /* Flush before writing in case this crashes */

	*address = value;

	return EC_SUCCESS;

}
DECLARE_CONSOLE_COMMAND(rw, command_read_word,
			"addr [value]",
			"Read or write a word in memory",
			NULL);

