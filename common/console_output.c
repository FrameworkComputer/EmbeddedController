/* Copyright 2012 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Console output module for Chrome EC */

#include "console.h"
#include "host_command.h"
#include "printf.h"
#include "uart.h"
#include "usb_console.h"
#include "util.h"

#ifdef CONFIG_CONSOLE_CHANNEL
/* Default to all channels active */
#ifndef CC_DEFAULT
#define CC_DEFAULT CC_ALL
#endif
static uint32_t channel_mask = CC_DEFAULT;
static uint32_t channel_mask_saved = CC_DEFAULT;

/*
 * List of channel names;
 *
 * We could try to get clever with #ifdefs or board-specific lists of channel
 * names, so that for example boards without port80 support don't waste binary
 * size on the channel name string for "port80".  Pruning the channel list
 * might also become more important if we have >32 channels - for example, if
 * we decide to replace enum console_channel with enum module_id.
 */
static const char *const channel_names[] = {
#define CONSOLE_CHANNEL(enumeration, string) string,
#include "console_channel.inc"
#undef CONSOLE_CHANNEL
};
BUILD_ASSERT(ARRAY_SIZE(channel_names) == CC_CHANNEL_COUNT);
/* ensure that we are not silently masking additional channels */
BUILD_ASSERT(CC_CHANNEL_COUNT <= 8 * sizeof(uint32_t));

static int console_channel_name_to_index(const char *name)
{
	int i;

	for (i = 0; i < CC_CHANNEL_COUNT; i++) {
		if (!strncasecmp(name, channel_names[i], strlen(name)))
			return i;
	}

	/* Not found */
	return -1;
}

void console_channel_enable(const char *name)
{
	int index = console_channel_name_to_index(name);

	if (index >= 0 && index != CC_COMMAND)
		channel_mask |= CC_MASK(index);
}
void console_channel_disable(const char *name)
{
	int index = console_channel_name_to_index(name);

	if (index >= 0 && index != CC_COMMAND)
		channel_mask &= ~CC_MASK(index);
}

bool console_channel_is_disabled(enum console_channel channel)
{
	if (!(CC_MASK(channel) & channel_mask))
		return true;
	return false;
}
#endif /* CONFIG_CONSOLE_CHANNEL */

#ifndef CONFIG_ZEPHYR
/*****************************************************************************/
/* Channel-based console output */

int cputs(enum console_channel channel, const char *outstr)
{
	int rv1, rv2;

	/* Filter out inactive channels */
	if (console_channel_is_disabled(channel))
		return EC_SUCCESS;

	rv1 = usb_puts(outstr);
	rv2 = uart_puts(outstr);

	return rv1 == EC_SUCCESS ? rv2 : rv1;
}

int cprintf(enum console_channel channel, const char *format, ...)
{
	int rv1, rv2;
	va_list args;

	/* Filter out inactive channels */
	if (console_channel_is_disabled(channel))
		return EC_SUCCESS;

	usb_va_start(args, format);
	rv1 = usb_vprintf(format, args);
	usb_va_end(args);

	va_start(args, format);
	rv2 = uart_vprintf(format, args);
	va_end(args);

	return rv1 == EC_SUCCESS ? rv2 : rv1;
}

int cprints(enum console_channel channel, const char *format, ...)
{
	int r, rv;
	va_list args;
	char ts_str[PRINTF_TIMESTAMP_BUF_SIZE];

	/* Filter out inactive channels */
	if (console_channel_is_disabled(channel))
		return EC_SUCCESS;

	snprintf_timestamp_now(ts_str, sizeof(ts_str));
	rv = cprintf(channel, "[%s ", ts_str);

	va_start(args, format);
	r = uart_vprintf(format, args);
	if (r)
		rv = r;
	va_end(args);

	usb_va_start(args, format);
	r = usb_vprintf(format, args);
	if (r)
		rv = r;
	usb_va_end(args);

	r = cputs(channel, "]\n");
	return r ? r : rv;
}
#endif /* CONFIG_ZEPHYR */

void cflush(void)
{
	uart_flush_output();
}

/*****************************************************************************/
/* Console commands */

#ifdef CONFIG_CONSOLE_CHANNEL
/* Set active channels */
static int command_ch(int argc, const char **argv)
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
			int index = console_channel_name_to_index(argv[1]);

			if (index >= 0) {
				if (console_channel_is_disabled(index)) {
					console_channel_enable(argv[1]);
					ccprintf("chan %s enabled\n", argv[1]);
				} else {
					console_channel_disable(argv[1]);
					ccprintf("chan %s disabled\n", argv[1]);
				}
			} else {
				int m = strtoi(argv[1], &e, 0);
				if (*e) {
					return EC_ERROR_PARAM1;
				}
				/* No disabling the command output channel */
				channel_mask = m | CC_MASK(CC_COMMAND);
			}

			return EC_SUCCESS;
		}
	}

	/* Print the list of channels */
	ccputs(" # Mask     E Channel\n");
	for (i = 0; i < CC_CHANNEL_COUNT; i++) {
		ccprintf("%2d %08x %c %s\n", i, CC_MASK(i),
			 (channel_mask & CC_MASK(i)) ? '*' : ' ',
			 channel_names[i]);
		cflush();
	}
	return EC_SUCCESS;
};
DECLARE_SAFE_CONSOLE_COMMAND(chan, command_ch,
			     "[ save | restore | <mask> | <name> ]",
			     "Save, restore, get or set console channel mask");
#endif /* CONFIG_CONSOLE_CHANNEL */

#ifdef CONFIG_HOSTCMD_CONSOLE_PRINT
static enum ec_status
host_command_console_print(struct host_cmd_handler_args *args)
{
	char *msg = (char *)args->params;

	if (args->params_size <= 0)
		return EC_RES_INVALID_PARAM;
	/* Ensure message is null terminated */
	msg[args->params_size - 1] = '\0';
	/* No response */
	args->response_size = 0;
	/* Print message to console */
	ccprints("Host: %s", msg);
	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_CONSOLE_PRINT, host_command_console_print,
		     EC_VER_MASK(0));
#endif
