/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Port 80 module for Chrome EC */

#include "board.h"
#include "console.h"
#include "host_command.h"
#include "port80.h"
#include "util.h"

#define CPRINTF(format, args...) cprintf(CC_PORT80, format, ## args)

#define HISTORY_LEN 256

static uint16_t history[HISTORY_LEN];
static int writes;  /* Number of port 80 writes so far */
static int last_boot; /* Last code from previous boot */
static int scroll;
static int print_in_int = 1;

void port_80_write(int data)
{
	/*
	 * Note that this currently prints from inside the LPC interrupt
	 * itself.  Probably not worth the system overhead to buffer the data
	 * and print it from a task, because we're printing a small amount of
	 * data and cprintf() doesn't block.
	 */
	if (print_in_int)
		CPRINTF("%c[%T Port 80: 0x%02x]", scroll ? '\n' : '\r', data);

	/* Save current port80 code if system is resetting */
	if (data == PORT_80_EVENT_RESET && writes) {
		int prev = history[(writes-1) % ARRAY_SIZE(history)];

		/* Ignore special event codes */
		if (prev < 0x100)
			last_boot = prev;
	}

	history[writes % ARRAY_SIZE(history)] = data;
	writes++;
}

/*****************************************************************************/
/* Console commands */

static int command_port80(int argc, char **argv)
{
	int head, tail;
	int printed = 0;
	int i;

	/*
	 * 'port80 scroll' toggles whether port 80 output begins with a newline
	 * (scrolling) or CR (non-scrolling).
	 */
	if (argc > 1) {
		if (!strcasecmp(argv[1], "scroll")) {
			scroll = !scroll;
			ccprintf("scroll %sabled\n", scroll ? "en" : "dis");
			return EC_SUCCESS;
		} else if (!strcasecmp(argv[1], "intprint")) {
			print_in_int = !print_in_int;
			ccprintf("printing in interrupt %sabled\n",
				 print_in_int ? "en" : "dis");
			return EC_SUCCESS;
		} else if (!strcasecmp(argv[1], "flush")) {
			writes = 0;
			return EC_SUCCESS;
		} else {
			return EC_ERROR_PARAM1;
		}
	}

	/*
	 * Print the port 80 writes so far, clipped to the length of our
	 * history buffer.
	 *
	 * Technically, if a port 80 write comes in while we're printing this,
	 * we could print an incorrect history.  Probably not worth the
	 * complexity to work around that.
	 */
	head = writes;
	if (head > ARRAY_SIZE(history))
		tail = head - ARRAY_SIZE(history);
	else
		tail = 0;

	ccputs("Port 80 writes:");
	for (i = tail; i < head; i++) {
		int e = history[i % ARRAY_SIZE(history)];
		switch (e) {
		case PORT_80_EVENT_RESUME:
			ccprintf("\n(S3->S0)");
			printed = 0;
			break;
		case PORT_80_EVENT_RESET:
			ccprintf("\n(RESET)");
			printed = 0;
			break;
		default:
			if (!(printed++ % 20)) {
				ccputs("\n ");
				cflush();
			}
			ccprintf(" %02x", e);
		}
	}
	ccputs(" <--new\n");
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(port80, command_port80,
			"[scroll | intprint | flush]",
			"Print port80 writes or toggle port80 scrolling",
			NULL);

int port80_last_boot(struct host_cmd_handler_args *args)
{
	struct ec_response_port80_last_boot *r = args->response;

	args->response_size = sizeof(*r);
	r->code = last_boot;

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_PORT80_LAST_BOOT,
		     port80_last_boot, EC_VER_MASK(0));
