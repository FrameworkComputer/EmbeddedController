/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* System module for Chrome EC */

#include "console.h"
#include "util.h"


static int command_write_word(int argc, char **argv)
{
	volatile uint32_t *address;
	uint32_t value;

	if (argc != 3)
		return EC_ERROR_INVAL;
	address = (uint32_t*)strtoi(argv[1], NULL, 0);
	value = strtoi(argv[2], NULL, 0);

	ccprintf("write 0x%p = 0x%08x\n", address, value);
	cflush();  /* Flush before writing in case this crashes */

	*address = value;

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(ww, command_write_word);
DECLARE_CONSOLE_COMMAND(writeword, command_write_word);


static int command_read_word(int argc, char **argv)
{
	volatile uint32_t *address;
	uint32_t value;

	if (argc != 2)
		return EC_ERROR_INVAL;

	address = (uint32_t*)strtoi(argv[1], NULL, 0);
	value = *address;

	ccprintf("read 0x%p = 0x%08x\n", address, value);

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(rw, command_read_word);
DECLARE_CONSOLE_COMMAND(readword, command_read_word);
