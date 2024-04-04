/* Copyright 2012 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* System module for Chrome EC */

#include "console.h"
#include "system.h"
#include "timer.h"
#include "util.h"
#include "watchdog.h"

enum format {
	FMT_WORD,
	FMT_HALF,
	FMT_BYTE,
	FMT_STRING,
};

#ifdef CONFIG_CMD_MD
static void show_val(uint32_t address, uint32_t index, enum format fmt)
{
	uint32_t val;
	uintptr_t ptr = address;

	switch (fmt) {
	case FMT_WORD:
		if (0 == (index % 4))
			ccprintf("\n%08X:", address + index * 4);
		val = *((uint32_t *)ptr + index);
		ccprintf(" %08x", val);
		break;
	case FMT_HALF:
		if (0 == (index % 8))
			ccprintf("\n%08X:", address + index * 2);
		val = *((uint16_t *)ptr + index);
		ccprintf(" %04x", val);
		break;
	case FMT_BYTE:
		if (0 == (index % 16))
			ccprintf("\n%08X:", address + index);
		val = *((uint8_t *)ptr + index);
		ccprintf(" %02x", val);
		break;
	case FMT_STRING:
		if (0 == (index % 32))
			ccprintf("\n%08X: ", address + index);
		val = *((uint8_t *)ptr + index);
		if (val >= ' ' && val <= '~')
			ccprintf("%c", val);
		else
			ccprintf("\\x%02x", val);
		break;
	}
	cflush();
}

static int command_mem_dump(int argc, const char **argv)
{
	uint32_t address, i, num = 1;
	char *e;
	enum format fmt = FMT_WORD;

#ifdef CONFIG_BOARD_FINGERPRINT
	if (system_is_locked())
		return EC_ERROR_ACCESS_DENIED;
#endif /* CONFIG_BOARD_FINGERPRINT */

	if (argc > 1) {
		if ((argv[1][0] == '.') && (strlen(argv[1]) == 2)) {
			switch (argv[1][1]) {
			case 'b':
				fmt = FMT_BYTE;
				break;
			case 'h':
				fmt = FMT_HALF;
				break;
			case 's':
				fmt = FMT_STRING;
				break;
			default:
				return EC_ERROR_PARAM1;
			}
			argc--;
			argv++;
		}
	}

	if (argc < 2)
		return EC_ERROR_PARAM_COUNT;

	address = strtoi(argv[1], &e, 0);
	if (*e)
		return EC_ERROR_PARAM1;

	if (argc >= 3)
		num = strtoi(argv[2], &e, 0);

	for (i = 0; i < num; i++) {
		show_val(address, i, fmt);
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

DECLARE_CONSOLE_COMMAND_FLAGS(
	md, command_mem_dump, "[.b|.h|.s] addr [count]",
	"dump memory values, optionally specifying the format",
	CMD_FLAG_RESTRICTED);
#endif /* CONFIG_CMD_MD */

#ifdef CONFIG_CMD_RW
static int command_read_word(int argc, const char **argv)
{
	volatile uint32_t *address;
	uint32_t value;
	unsigned int access_size = 4;
	unsigned int argc_offs = 0;
	char *e;

#ifdef CONFIG_BOARD_FINGERPRINT
	if (system_is_locked())
		return EC_ERROR_ACCESS_DENIED;
#endif /* CONFIG_BOARD_FINGERPRINT */

	if (argc < 2)
		return EC_ERROR_PARAM_COUNT;

	if (argc > 2) {
		if ((argv[1][0] == '.') && (strlen(argv[1]) == 2)) {
			argc_offs = 1;
			switch (argv[1][1]) {
			case 'b':
				access_size = 1;
				break;
			case 'h':
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
			ccprintf("read 0x%p = 0x%02x\n", address,
				 *((uint8_t *)address));
			break;
		case 2:
			ccprintf("read 0x%p = 0x%04x\n", address,
				 *((uint16_t *)address));
			break;

		default:
			ccprintf("read 0x%p = 0x%08x\n", address, *address);
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
		cflush(); /* Flush before writing in case this crashes */
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

DECLARE_CONSOLE_COMMAND_FLAGS(
	rw, command_read_word, "[.b|.h] addr [value]",
	"Read or write a word in memory optionally specifying the size",
	CMD_FLAG_RESTRICTED);
#endif /* CONFIG_CMD_RW */
