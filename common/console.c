/* Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Console module for Chrome EC */

#include "console.h"
#include "task.h"
#include "uart.h"
#include "registers.h"
#include "util.h"

#define MAX_COMMAND_GROUPS 20
#define MAX_ARGS_PER_COMMAND 10

#define PROMPT "> "

static const struct console_group *group_list[MAX_COMMAND_GROUPS];
static int group_count = 0;


void console_has_input(void)
{
	/* Wake up the console task */
	task_send_msg(TASK_ID_CONSOLE, TASK_ID_CONSOLE, 0);
}


int console_register_commands(const struct console_group *group)
{
	if (group_count >= MAX_COMMAND_GROUPS)
		return EC_ERROR_OVERFLOW;

	group_list[group_count++] = group;
	return EC_SUCCESS;
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
	int c, g;

	/* Find the command in the command groups */
	for (g = 0; g < group_count; g++) {
		cmd = group_list[g]->commands;
		for (c = group_list[g]->command_count; c > 0; c--, cmd++) {
			if (!strcasecmp(name, cmd->name))
				return cmd;
		}
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


static char last_input[80];
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

		/* "." repeats the last command, if any */
		if (!strcasecmp(input_buf, ".\n"))
			strzcpy(input_buf, last_input, sizeof(input_buf));
		else if (!isspace(*input_buf))
			strzcpy(last_input, input_buf, sizeof(last_input));

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
	int c, g;

	uart_puts("Known commands:\n");

	for (g = 0; g < group_count; g++) {
		cmd = group_list[g]->commands;
		uart_printf("Group %s:\n", group_list[g]->group_name);
		for (c = group_list[g]->command_count; c > 0; c--, cmd++)
			uart_printf("  %s\n", cmd->name);
		/* Generates enough output to overflow the buffer */
		uart_flush_output();
	}

	uart_puts("'.' repeats the last command.\n");

	return EC_SUCCESS;
}

static const struct console_command console_commands[] = {
	{"help", command_help},
	{"?", command_help},
};
static const struct console_group command_group = {
	"Console", console_commands, ARRAY_SIZE(console_commands)
};

/*****************************************************************************/
/* Initialization */

int console_init(void)
{
	*last_input = '\0';
	*input_buf = '\0';
	uart_set_console_mode(1);
	uart_printf("Console is enabled; type HELP for help.\n");
	uart_puts(PROMPT);
	/* Register our internal commands */
	return console_register_commands(&command_group);
}
