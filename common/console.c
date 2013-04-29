/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Console module for Chrome EC */

#include "console.h"
#include "link_defs.h"
#include "system.h"
#include "task.h"
#include "uart.h"
#include "util.h"

#define MAX_ARGS_PER_COMMAND 10

#define PROMPT "> "

/* Current console command line */
static char input_buf[CONSOLE_INPUT_LINE_SIZE];

/**
 * Split a line of input into words.
 *
 * @param input		Input line; modified to add nulls after each word.
 * @param argc		Destination for number of words.
 * @param argv		Destination array for pointers to words; must be at
 *			least MAX_ARGS_PER_COMMAND entries long.
 *
 * @return EC_SUCCESS.  If more than MAX_ARGS_PER_COMMAND words are found,
 *	discards the excess and returns EC_ERROR_OVERFLOW.
 */
static int split_words(char *input, int *argc, char **argv)
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
			/* Comments start with hash and go to end of line */
			break;
		} else if (!in_word) {
			/* Starting a new word */
			if (*argc >= MAX_ARGS_PER_COMMAND)
				return EC_ERROR_OVERFLOW;

			argv[*argc] = c;
			in_word = 1;
		}
	}
	return EC_SUCCESS;
}

/**
 * Find a command by name.
 *
 * Allows partial matches, as long as the partial match is unique to one
 * command.  So "foo" will match "foobar" as long as there isn't also a
 * command "food".
 *
 * @param name		Command name to find.
 *
 * @return A pointer to the command structure, or NULL if no match found.
 */
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

/**
 * Handle a line of input containing a single command.
 *
 * @param input		Input buffer; modified during parsing.
 *
 * @return EC_SUCCESS, or non-zero if error.
 */
static int handle_command(char *input)
{
	const struct console_command *cmd;
	char *argv[MAX_ARGS_PER_COMMAND];
	int argc = 0;
	int rv;

	/* Split input into words.  Ignore words past our limit. */
	split_words(input, &argc, argv);

	/* If no command, nothing to do */
	if (!argc)
		return EC_SUCCESS;

	cmd = find_command(argv[0]);
	if (!cmd) {
		ccprintf("Command '%s' not found or ambiguous.\n", argv[0]);
		return EC_ERROR_UNKNOWN;
	}

	rv = cmd->handler(argc, argv);
	if (rv == EC_SUCCESS)
		return rv;

	/* Print more info for errors */
	if (rv == EC_ERROR_INVAL)
		ccputs("Command usage/param invalid.\n");
	else if (rv == EC_ERROR_PARAM_COUNT)
		ccputs("Wrong number of params.\n");
	else if (rv >= EC_ERROR_PARAM1 && rv <= EC_ERROR_PARAM9)
		ccprintf("Parameter %d invalid.\n", rv - EC_ERROR_PARAM1 + 1);
	else if (rv != EC_SUCCESS) {
		ccprintf("Command returned error %d\n", rv);
		return rv;
	}

#ifdef CONFIG_CONSOLE_CMDHELP
	if (cmd->argdesc)
		ccprintf("Usage: %s %s\n", cmd->name, cmd->argdesc);
#endif
	return rv;
}

static void console_init(void)
{
	*input_buf = '\0';
	ccprintf("Console is enabled; type HELP for help.\n");
	ccputs(PROMPT);
}

static void console_process(void)
{
	/*
	 * Process all pending console commands.  Need to do this all at once
	 * since our interrupt may have been triggered multiple times.
	 */
	while (uart_peek('\n') >= 0) {
		uart_gets(input_buf, sizeof(input_buf));
		handle_command(input_buf);
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
#ifdef CONFIG_CONSOLE_RESTRICTED_INPUT
	/* the console is not available due to security restrictions */
	if (system_is_locked()) {
		ccprintf("Console is DISABLED (WP is ON).\n");
		while (1)
			task_wait_event(-1);
	}
#endif

	console_init();

	while (1) {
		console_process();
		/* Wait for the next command message */
		task_wait_event(-1);
	}
}

/*****************************************************************************/
/* Console commands */

/* Command handler - prints help. */
static int command_help(int argc, char **argv)
{
	const int ncmds = __cmds_end - __cmds;
	const int cols = 5;  /* printing in five columns */
	const int rows = (ncmds + cols - 1) / cols;
	int i, j;

#ifdef CONFIG_CONSOLE_CMDHELP
	if (argc == 2) {
		const struct console_command *cmd;

		if (!strcasecmp(argv[1], "list")) {
			ccputs("Known commands:\n");
			for (i = 0; i < ncmds; i++) {
				ccprintf("  %-15s%s\n",
					 __cmds[i].name, __cmds[i].shorthelp);
				cflush();
			}
			ccputs("HELP CMD = help on CMD.\n");
			return EC_SUCCESS;
		}
		cmd = find_command(argv[1]);
		if (!cmd) {
			ccprintf("Command '%s' not found or ambiguous.\n",
				 argv[1]);
			return EC_ERROR_UNKNOWN;
		}
		ccprintf("Usage: %s %s\n", cmd->name,
			 (cmd->argdesc ? cmd->argdesc : ""));
		if (cmd->shorthelp)
			ccprintf("%s\n", cmd->shorthelp);
		return EC_SUCCESS;
	}
#endif

	ccputs("Known commands:\n");
	for (i = 0; i < rows; i++) {
		ccputs("  ");
		for (j = 0; j < cols; j++) {
			int index = j * rows + i;
			if (index >= ncmds)
				break;
			ccprintf("%-15s", __cmds[index].name);
		}
		ccputs("\n");
		cflush();
	}

#ifdef CONFIG_CONSOLE_CMDHELP
	ccputs("HELP LIST = more info; ");
	ccputs("HELP CMD = help on CMD.\n");
#endif

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(help, command_help,
			"[ list | <name> ]",
			"Print command help",
			NULL);
