/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Console module for Chrome EC */

#include "console.h"
#include "link_defs.h"
#include "task.h"
#include "uart.h"
#include "util.h"

#define MAX_ARGS_PER_COMMAND 10

#define PROMPT "> "

/* Default to all channels active */
#ifndef CC_DEFAULT
#define CC_DEFAULT	CC_ALL
#endif
static uint32_t channel_mask = CC_DEFAULT;

static char input_buf[80];  /* Current console command line */

/* List of channel names; must match enum console_channel. */
/* TODO: move this to board.c */
static const char *channel_names[CC_CHANNEL_COUNT] = {
	"command",
	"charger",
	"chipset",
	"dma",
	"gpio",
	"hostcmd",
	"i2c",
	"i8042",
	"keyboard",
	"keyscan",
	"lightbar",
	"lpc",
	"port80",
	"powerbtn",
	"pwm",
	"system",
	"task",
	"usbcharge",
	"vboot",
};

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


void cflush(void)
{
	uart_flush_output();
}



/*****************************************************************************/
/* Console input */

/* Splits a line of input into words.  Stores the count of words in
 * <argc>.  Stores pointers to the words in <argv>, which must be at
 * least <max_argc> long.  If more than <max_argc> words are found,
 * discards the excess and returns EC_ERROR_OVERFLOW. */
static int split_words(char *input, int max_argc, int *argc, char **argv)
{
	char *c;
	int in_word = 0;

	/* Parse input into words */
	*argc = 0;
	for (c = input; *c; c++) {
		if (isspace(*c)) {
			if (in_word) {
				/* Ending a word */
				*c = '\0';
				++*argc;
				in_word = 0;
			}
		} else if (*c == '#') {
			/* After the hash sign is comment, ignored.
			 * TODO: Need more logic to suuport escaping. */
			break;
		} else {
			if (!in_word) {
				/* Starting a new word */
				if (*argc >= max_argc)
					return EC_ERROR_OVERFLOW;

				argv[*argc] = c;
				in_word = 1;
			}
		}
	}
	return EC_SUCCESS;
}


/* Finds a command by name.  Returns the command structure, or NULL if
 * no match found. */
static const struct console_command *find_command(char *name)
{
	const struct console_command *cmd, *match = NULL;
	int match_length = strlen(name);

	for (cmd = __cmds; cmd < __cmds_end; cmd++) {
		if (!strncasecmp(name, cmd->name, match_length)) {
			if (match)
				return NULL;
			match = cmd;
		}
	}

	return match;
}


/* Handles a line of input containing a single command.
 *
 * Modifies the input string during parsing. */
static int handle_command(char *input)
{
	const struct console_command *cmd;
	char *argv[MAX_ARGS_PER_COMMAND];
	int argc = 0;

	/* Split input into words.  Ignore words past our limit. */
	split_words(input, MAX_ARGS_PER_COMMAND, &argc, argv);

	/* If no command, nothing to do */
	if (!argc)
		return EC_SUCCESS;

	cmd = find_command(argv[0]);
	if (cmd)
		return cmd->handler(argc, argv);

	ccprintf("Command '%s' either not found or ambiguous.\n", argv[0]);
	return EC_ERROR_UNKNOWN;
}


static int console_init(void)
{
	*input_buf = '\0';
	uart_set_console_mode(1);
	ccprintf("Console is enabled; type HELP for help.\n");
	ccputs(PROMPT);

	/* TODO: restore channel list from EEPROM */

	return EC_SUCCESS;
}


/* handle a console command */
static void console_process(void)
{
        int rv;

	/* Process all the pending commands.  Need to do this all at once
	 * since our interrupt may have been triggered multiple times. */
	/* TODO: Go to sleep briefly between commands to give lower
	 * priority tasks a chance to run? */
	while (uart_peek('\n') >= 0) {
		uart_gets(input_buf, sizeof(input_buf));

		rv = handle_command(input_buf);
                if (rv != EC_SUCCESS)
			ccprintf("Command returned error %d\n", rv);
		ccputs(PROMPT);
	}
}


void console_has_input(void)
{
	/* Wake up the console task */
	task_wake(TASK_ID_CONSOLE);
}


void console_task(void)
{
	console_init();

	while (1) {
		console_process();
		/* wait for the next command message */
		task_wait_event(-1);
	}
}


/*****************************************************************************/
/* Console commands */

/* Command handler - prints help. */
static int command_help(int argc, char **argv)
{
	const struct console_command *cmd;
	const int ncmds = ((uint32_t)__cmds_end - (uint32_t)__cmds) /
		sizeof(struct console_command);
	const char *prev = " ";
	int i;

	ccputs("Known commands:");

	/* Sort the commands by name */
	for (i = 0; i < ncmds; i++) {
		const char *next = "zzzz";

		if (!(i % 5))
			ccputs("\n  ");

		/* Find the next command */
		for (cmd = __cmds; cmd < __cmds_end; cmd++) {
			if (strcasecmp(prev, cmd->name) < 0 &&
			    strcasecmp(cmd->name, next) < 0)
				next = cmd->name;
		}

		ccprintf("%-15s", next);
		/* Generates enough output to overflow the buffer */
		cflush();

		prev = next;
	}

	ccputs("\n");
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(help, command_help);


/* Set active channels */
static int command_ch(int argc, char **argv)
{
	int m;
	char *e;

	/* If no args, print the list of channels */
	if (argc == 1) {
		int i;
		ccputs(" # Mask     Enabled Channel\n");
		for (i = 0; i < CC_CHANNEL_COUNT; i++) {
			ccprintf("%2d %08x %c       %s\n",
				 i, CC_MASK(i),
				 (channel_mask & CC_MASK(i)) ? '*' : ' ',
				 channel_names[i]);
			cflush();
		}
		return EC_SUCCESS;
	}

	/* If one arg, set the mask */
	if (argc == 2) {
		m = strtoi(argv[1], &e, 0);
		if (e && *e) {
			ccputs("Invalid mask\n");
			return EC_ERROR_INVAL;
		}
		/* No disabling the command output channel */
		channel_mask = m | CC_MASK(CC_COMMAND);

		/* TODO: save channel list to EEPROM */

		return EC_SUCCESS;
	}

	/* Otherwise, print help */
	ccputs("Usage: chan [newmask]\n");
	return EC_ERROR_INVAL;
};
DECLARE_CONSOLE_COMMAND(chan, command_ch);
