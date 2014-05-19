/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Console output module for Chrome EC */

#include "console.h"
#include "uart.h"
#include "util.h"

/* Default to all channels active */
#ifndef CC_DEFAULT
#define CC_DEFAULT CC_ALL
#endif
static uint32_t channel_mask = CC_DEFAULT;
static uint32_t channel_mask_saved = CC_DEFAULT;

/*
 * List of channel names; must match enum console_channel.
 *
 * We could do something fancy and macro-y with this like ec.tasklist, so that
 * the channel name list and console_channel enum come from the same header
 * file.  That's clever, but I'm not convinced it's more readable or
 * maintainable than the two simple lists we have now.
 *
 * We could also try to get clever with #ifdefs or board-specific lists of
 * channel names, so that for example boards without port80 support don't waste
 * binary size on the channel name string for "port80".  Pruning the channel
 * list might also become more important if we have >32 channels - for example,
 * if we decide to replace enum console_channel with enum module_id.
 */
static const char * const channel_names[] = {
	"command",
	"accel",
	"charger",
	"chipset",
	"clock",
	"dma",
	"events",
	"gpio",
	"hostcmd",
	"i2c",
	"keyboard",
	"keyscan",
	"lidangle",
	"lightbar",
	"lpc",
	"motionsense",
	"port80",
	"pwm",
	"spi",
	"switch",
	"system",
	"task",
	"thermal",
	"usbcharge",
	"usbpd",
	"vboot",
	"hook",
};
BUILD_ASSERT(ARRAY_SIZE(channel_names) == CC_CHANNEL_COUNT);

/*****************************************************************************/
/* Channel-based console output */

int cputs(enum console_channel channel, const char *outstr)
{
	/* Filter out inactive channels */
	if (!(CC_MASK(channel) & channel_mask))
		return EC_SUCCESS;

	return uart_puts(outstr);
}

int cprintf(enum console_channel channel, const char *format, ...)
{
	int rv;
	va_list args;

	/* Filter out inactive channels */
	if (!(CC_MASK(channel) & channel_mask))
		return EC_SUCCESS;

	va_start(args, format);
	rv = uart_vprintf(format, args);
	va_end(args);
	return rv;
}

int cprints(enum console_channel channel, const char *format, ...)
{
	int r, rv;
	va_list args;

	/* Filter out inactive channels */
	if (!(CC_MASK(channel) & channel_mask))
		return EC_SUCCESS;

	va_start(args, format);
	rv = uart_printf("[%T ");
	r = uart_vprintf(format, args);
	if (r)
		rv = r;
	r = uart_puts("]\n");
	va_end(args);
	return r ? r : rv;
}

void cflush(void)
{
	uart_flush_output();
}

/*****************************************************************************/
/* Console commands */

/* Set active channels */
static int command_ch(int argc, char **argv)
{
	int i;
	char *e;

	/* If one arg, save / restore, or set the mask */
	if (argc == 2) {
		if (strcasecmp(argv[1], "save") == 0) {
			channel_mask_saved = channel_mask;
			return EC_SUCCESS;
		} else if (strcasecmp(argv[1], "restore") == 0) {
			channel_mask = channel_mask_saved;
			return EC_SUCCESS;

		} else {
			/* Set the mask */
			int m = strtoi(argv[1], &e, 0);
			if (*e)
				return EC_ERROR_PARAM1;

			/* No disabling the command output channel */
			channel_mask = m | CC_MASK(CC_COMMAND);

			return EC_SUCCESS;
		}
	}

	/* Print the list of channels */
	ccputs(" # Mask     E Channel\n");
	for (i = 0; i < CC_CHANNEL_COUNT; i++) {
		ccprintf("%2d %08x %c %s\n",
			 i, CC_MASK(i),
			 (channel_mask & CC_MASK(i)) ? '*' : ' ',
			 channel_names[i]);
		cflush();
	}
	return EC_SUCCESS;
};
DECLARE_CONSOLE_COMMAND(chan, command_ch,
			"[ save | restore | <mask> ]",
			"Save, restore, get or set console channel mask",
			NULL);
