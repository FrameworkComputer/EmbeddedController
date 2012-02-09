/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Console module for Chrome EC */

#include "console.h"
#include "task.h"
#include "uart.h"
#include "util.h"

#define MAX_ARGS_PER_COMMAND 10

#define PROMPT "> "

/* Console commands are described in a special section */
extern const struct console_command __cmds[];
extern const struct console_command __cmds_end[];

void console_has_input(void)
{
	/* Wake up the console task */
	task_send_msg(TASK_ID_CONSOLE, TASK_ID_CONSOLE, 0);
}


/* Splits a line of input into words.  Stores the count of words in
 * <argc>.  Stores pointers to the words in <argv>, which must be at
 * least <max_argc> long.  If more than <max_argc> words are found,
 * discards the excess and returns EC_ERROR_OVERFLOW. */
int split_words(char *input, int max_argc, int *argc, char **argv)
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
const struct console_command *find_command(char *name)
{
	const struct console_command *cmd;

	for (cmd = __cmds; cmd < __cmds_end; cmd++) {
		if (!strcasecmp(name, cmd->name))
			return cmd;
	}

	return NULL;
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

	uart_printf("Command '%s' not found.\n", argv[0]);
	return EC_ERROR_UNKNOWN;
}


static char input_buf[80];

/* handle a console command */
void console_process(void)
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
                  uart_printf("Command returned error %d\n", rv);
		uart_puts(PROMPT);
	}
}

void console_task(void)
{
	console_init();

	while (1) {
		console_process();
		/* wait for the next command message */
		task_wait_msg(-1);
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

	uart_puts("Known commands:");

	/* Sort the commands by name */
	for (i = 0; i < ncmds; i++) {
		const char *next = "zzzz";

		if (!(i % 5))
			uart_puts("\n  ");

		/* Find the next command */
		for (cmd = __cmds; cmd < __cmds_end; cmd++) {
			if (strcasecmp(prev, cmd->name) < 0 &&
			    strcasecmp(cmd->name, next) < 0)
				next = cmd->name;
		}

		uart_printf("%-15s", next);
		/* Generates enough output to overflow the buffer */
		uart_flush_output();

		prev = next;
	}

	uart_puts("\n");
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(help, command_help);

/*****************************************************************************/
/* Initialization */

int console_init(void)
{
	*input_buf = '\0';
	uart_set_console_mode(1);
	uart_printf("Console is enabled; type HELP for help.\n");
	uart_puts(PROMPT);

	return EC_SUCCESS;
}
