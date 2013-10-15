/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Console module for Chrome EC */
#include "clock.h"
#include "console.h"
#include "link_defs.h"
#include "system.h"
#include "task.h"
#include "uart.h"
#include "util.h"

#define MAX_ARGS_PER_COMMAND 10

#define PROMPT "> "

/* ASCII control character; for example, CTRL('C') = ^C */
#define CTRL(c) ((c) - '@')

#ifdef CONFIG_CONSOLE_HISTORY
/* History buffers */
static char history[CONFIG_CONSOLE_HISTORY][CONFIG_CONSOLE_INPUT_LINE_SIZE];
static int history_next, history_pos;
#endif

/* Current console command line */
static char input_buf[CONFIG_CONSOLE_INPUT_LINE_SIZE];

/* Length of current line */
static int input_len;

/* Cursor position in current line */
static int input_pos;

/* Was last received character a carriage return? */
static int last_rx_was_cr;

/* State of input escape code */
static enum {
	ESC_OUTSIDE,   /* Not in escape code */
	ESC_START,     /* Got ESC */
	ESC_BAD,       /* Bad escape sequence */
	ESC_BRACKET,   /* Got ESC [ */
	ESC_BRACKET_1, /* Got ESC [ 1 */
	ESC_BRACKET_3, /* Got ESC [ 3 */
	ESC_O,         /* Got ESC O */
} esc_state;

/* Extended key code values, from multi-byte escape sequences */
enum extended_key_code {
	KEY_UP_ARROW = 0x100,
	KEY_DOWN_ARROW,
	KEY_RIGHT_ARROW,
	KEY_LEFT_ARROW,
	KEY_END,
	KEY_HOME,
	KEY_DEL
};

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
	int in_line = 1;

	/* Parse input into words */
	*argc = 0;
	for (c = input; in_line; c++) {
		if (!*c)
			in_line = 0;
		if (isspace(*c) || !*c) {
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
			/*
			 * Check if 'cmd->name' is of the same length as
			 * 'name'. If yes, then we have a full match.
			 */
			if (cmd->name[match_length] == '\0')
				return cmd;
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

static void move_cursor_right(void)
{
	if (input_pos == input_len)
		return;

	ccputs("\x1b[1C");
	input_pos++;
}

static void move_cursor_end(void)
{
	if (input_pos == input_len)
		return;

	ccprintf("\x1b[%dC", input_len - input_pos);
	input_pos = input_len;
}

static void move_cursor_left(void)
{
	if (input_pos == 0)
		return;

	ccputs("\x1b[1D");
	input_pos--;
}

static void move_cursor_begin(void)
{
	if (input_pos == 0)
		return;

	ccprintf("\x1b[%dD", input_pos);
	input_pos = 0;
}

static void repeat_char(char c, int cnt)
{
	while (cnt--)
		uart_putc(c);
}

#ifdef CONFIG_CONSOLE_HISTORY

/**
 * Load input history
 *
 * @param idx		History index to load
 */
static void load_history(int idx)
{
	/* Copy history */
	strzcpy(input_buf, history[idx], CONFIG_CONSOLE_INPUT_LINE_SIZE);

	/* Print history */
	move_cursor_begin();
	ccputs(input_buf);

	/* Clear everything past end of history */
	input_pos = strlen(input_buf);
	if (input_len > input_pos) {
		repeat_char(' ', input_len - input_pos);
		repeat_char('\b', input_len - input_pos);
	}
	input_len = input_pos;
}

/**
 * Save line to the next history slot
 */
static void save_history(void)
{
	strzcpy(history[history_next], input_buf,
		CONFIG_CONSOLE_INPUT_LINE_SIZE);
}

#endif /* CONFIG_CONSOLE_HISTORY */

static void handle_backspace(void)
{
	if (!input_pos)
		return;  /* Already at beginning of line */

	/* Move cursor back */
	uart_putc('\b');

	/* Print and move anything following the cursor position */
	if (input_pos != input_len) {
		ccputs(input_buf + input_pos);
		memmove(input_buf + input_pos - 1,
			input_buf + input_pos,
			input_len - input_pos + 1);
	} else {
		input_buf[input_len - 1] = '\0';
	}

	/* Space over last character and move cursor to correct position */
	uart_putc(' ');
	repeat_char('\b', input_len - input_pos + 1);

	input_len--;
	input_pos--;
}

/**
 * Escape code handler
 *
 * @param c             Next received character.
 * @return		Key code, or -1 if character was eaten
 */
static int handle_esc(int c)
{
	switch (esc_state) {
	case ESC_START:
		if (c == '[') {
			esc_state = ESC_BRACKET;
			return -1;
		} else if (c == 'O') {
			esc_state = ESC_O;
			return -1;
		}
		break;

	case ESC_BRACKET:
		if (c == '1') {
			esc_state = ESC_BRACKET_1;
			return -1;
		} else if (c == '3') {
			esc_state = ESC_BRACKET_3;
			return -1;
		}

		if (c == 'A')
			return KEY_UP_ARROW;
		else if (c == 'B')
			return KEY_DOWN_ARROW;
		else if (c == 'C')
			return KEY_RIGHT_ARROW;
		else if (c == 'D')
			return KEY_LEFT_ARROW;
		break;

	case ESC_O:
		if (c == 'F')
			return KEY_END;
		break;

	case ESC_BRACKET_1:
		if (c == '~')
			return KEY_HOME;
		break;

	case ESC_BRACKET_3:
		if (c == '~')
			return KEY_DEL;
		break;

	default:
		break;
	}

	/* Check if the escape code is done */
	if (isalpha(c) || c == '~')
		esc_state = ESC_OUTSIDE;
	else
		esc_state = ESC_BAD;

	return -1;
}

static void console_handle_char(int c)
{
	/* Translate CR and CRLF to LF (newline) */
	if (c == '\r') {
		last_rx_was_cr = 1;
		c = '\n';
	} else if (c == '\n' && last_rx_was_cr) {
		last_rx_was_cr = 0;
		return;
	} else {
		last_rx_was_cr = 0;
	}

	/* Handle terminal escape sequences (ESC [ ...) */
	if (c == 0x1B) {
		esc_state = ESC_START;
		return;
	} else if (esc_state) {
		c = handle_esc(c);
		if (c != -1)
			esc_state = ESC_OUTSIDE;
	}

	switch (c) {
	case KEY_DEL:
		if (input_pos == input_len)
			break;  /* Already at end */

		move_cursor_right();

		/* Drop through to backspace handling */
	case '\b':
	case 0x7f:
		handle_backspace();
		break;

	case '\n':
		/* Terminate this line */
		uart_puts("\r\n");

#ifdef CONFIG_CONSOLE_HISTORY
		/* Save command in history buffer */
		if (input_len) {
			save_history();
			history_next = (history_next + 1) %
				CONFIG_CONSOLE_HISTORY;
			history_pos = history_next;
		}
#endif

		/* Handle command */
		handle_command(input_buf);

		/* Start new line */
		input_pos = input_len = 0;
		input_buf[0] = '\0';

		/* Reprint prompt */
		ccputs(PROMPT);
		break;

	case CTRL('A'):
	case KEY_HOME:
		move_cursor_begin();
		break;

	case CTRL('B'):
	case KEY_LEFT_ARROW:
		move_cursor_left();
		break;

	case CTRL('E'):
	case KEY_END:
		move_cursor_end();
		break;

	case CTRL('F'):
	case KEY_RIGHT_ARROW:
		move_cursor_right();
		break;

	case CTRL('K'):
		/* Kill to end of line */
		if (input_pos == input_len)
			break;

		repeat_char(' ', input_len - input_pos);
		repeat_char('\b', input_len - input_pos);
		input_len = input_pos;
		input_buf[input_len] = '\0';
		break;

	case CTRL('L'):
		/* Reprint current */
		ccputs("\x0c" PROMPT);
		ccputs(input_buf);
		repeat_char('\b', input_len - input_pos);
		break;

#ifdef CONFIG_CONSOLE_HISTORY

	case CTRL('P'):
	case KEY_UP_ARROW:
		/* History previous */
		if (history_pos == history_next)
			save_history();

		if (--history_pos < 0)
			history_pos = CONFIG_CONSOLE_HISTORY - 1;

		load_history(history_pos);
		break;

	case CTRL('N'):
	case KEY_DOWN_ARROW:
		/* History next */
		if (history_pos == history_next)
			save_history();

		if (++history_pos >= CONFIG_CONSOLE_HISTORY)
			history_pos = 0;

		load_history(history_pos);
		break;

#endif /* CONFIG_CONSOLE_HISTORY */

	default:
		/* Ignore non-printing characters */
		if (!isprint(c))
			break;

		/* Ignore if line is full (leaving room for terminating null) */
		if (input_len >= sizeof(input_buf) - 1)
			break;

		/* Print character */
		uart_putc(c);

		/* If not at end of line, print rest of line and move it down */
		if (input_pos != input_len) {
			ccputs(input_buf + input_pos);
			memmove(input_buf + input_pos + 1,
				input_buf + input_pos,
				input_len - input_pos + 1);
			repeat_char('\b', input_len - input_pos);
		}

		/* Add character to buffer and terminate it */
		input_buf[input_pos++] = c;
		input_buf[++input_len] = '\0';
	}
}

void console_has_input(void)
{
#ifdef CONFIG_LOW_POWER_IDLE
	/* Notify the clock module that the console is in use. */
	clock_refresh_console_in_use();
#endif

	/* Wake up the console task */
	task_wake(TASK_ID_CONSOLE);
}

void console_task(void)
{
#ifdef CONFIG_CONSOLE_RESTRICTED_INPUT
	/* the console is not available due to security restrictions */
	if (system_is_locked() && !system_get_console_force_enabled()) {
		ccprintf("Console is DISABLED (WP is ON).\n");
		while (1)
			task_wait_event(-1);
	}
#endif

	console_init();

	while (1) {
		int c = uart_getc();

		if (c == -1)
			task_wait_event(-1);  /* Wait for more input */
		else
			console_handle_char(c);
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

#ifdef CONFIG_CONSOLE_RESTRICTED_INPUT
static int command_force_enabled(int argc, char **argv)
{
	int val;

	if (argc < 2)
		return EC_ERROR_PARAM_COUNT;

	if (!parse_bool(argv[1], &val))
		return EC_ERROR_PARAM1;

	system_set_console_force_enabled(val);
	ccprintf("Console force enabled = %s\n", val ? "on" : "off");

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(forceen, command_force_enabled,
			"<on | off>",
			"Force enable console",
			NULL);
#endif

#ifdef CONFIG_CONSOLE_HISTORY
static int command_history(int argc, char **argv)
{
	int i;

	for (i = 0; i < CONFIG_CONSOLE_HISTORY; i++) {
		int idx = (history_next + i) % CONFIG_CONSOLE_HISTORY;
		if (history[idx][0])
			ccprintf("%s\n", history[idx]);
	}

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(history, command_history,
			NULL,
			"Print console history",
			NULL);
#endif
