/* Copyright 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Port 80 module for Chrome EC */

#include "common.h"
#include "console.h"
#include "display_7seg.h"
#include "hooks.h"
#include "host_command.h"
#include "port80.h"
#include "task.h"
#include "timer.h"
#include "util.h"

#define CPRINTF(format, args...) cprintf(CC_PORT80, format, ## args)

static uint16_t __bss_slow history[CONFIG_PORT80_HISTORY_LEN];
static int __bss_slow writes;    /* Number of port 80 writes so far */
static int last_boot; /* Last code from previous boot */
static int __bss_slow scroll;

#ifdef CONFIG_BRINGUP
#undef CONFIG_PORT80_PRINT_IN_INT
#define CONFIG_PORT80_PRINT_IN_INT 1
#endif

static int print_in_int = CONFIG_PORT80_PRINT_IN_INT;

static void port80_dump_buffer(void);
DECLARE_DEFERRED(port80_dump_buffer);

void port_80_write(int data)
{
	/*
	 * By default print_in_int is disabled if:
	 * 1. CONFIG_BRINGUP is not defined
	 * 2. CONFIG_PRINT_IN_INT is set to disable by default
	 *
	 * This is done to prevent printing in interrupt context. Boards can
	 * enable this by either defining CONFIG_BRINGUP or enabling
	 * CONFIG_PRINT_IN_INT in board configs.
	 *
	 * If at runtime, print_in_int is disabled, then this function will
	 * schedule a deferred call 4 seconds after the last port80 write to
	 * dump the current port80 buffer to EC console. This is to allow
	 * developers to help debug BIOS progress by tracing port80 messages.
	 *
	 * P.S.: Deferred call is not scheduled for special event codes (data >=
	 * 0x100). This is because only 8-bit port80 messages are assumed to be
	 * coming from the host.
	 */
#ifndef CONFIG_CUSTOMER_PORT80
	if (print_in_int)
		CPRINTF("%c[%pT Port 80: 0x%02x]",
			scroll ? '\n' : '\r', PRINTF_TIMESTAMP_NOW, data);
	else if (data < 0x100)
		hook_call_deferred(&port80_dump_buffer_data, 4 * SECOND);
#else
	/* this is for customer design to show port80 on 7-segment display */
	CPRINTF("PORT80: 00%02X\n", data);
#endif

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

static void port80_dump_buffer(void)
{
	int printed = 0;
	int i;
	int head, tail;
	int last_e = 0;

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
			last_e = e;
		}
	}
	ccputs(" <--new\n");

	/* Displaying last port80 msg on 7-segment if it is enabled */
	if (IS_ENABLED(CONFIG_SEVEN_SEG_DISPLAY) && last_e)
		display_7seg_write(SEVEN_SEG_PORT80_DISPLAY, last_e);
}

int port_80_last(void)
{
	return history[(writes-1) % ARRAY_SIZE(history)];
}

/*****************************************************************************/
/* Console commands */

static int command_port80(int argc, char **argv)
{
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

	port80_dump_buffer();
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(port80, command_port80,
			"[scroll | intprint | flush]",
			"Print port80 writes or toggle port80 scrolling");

enum ec_status port80_last_boot(struct host_cmd_handler_args *args)
{
	struct ec_response_port80_last_boot *r = args->response;

	args->response_size = sizeof(*r);
	r->code = last_boot;

	return EC_RES_SUCCESS;
}

enum ec_status port80_command_read(struct host_cmd_handler_args *args)
{
	const struct ec_params_port80_read *p = args->params;
	uint32_t offset = p->read_buffer.offset;
	uint32_t entries = p->read_buffer.num_entries;
	int i;
	struct ec_response_port80_read *rsp = args->response;

	if (args->version == 0)
		return port80_last_boot(args);

	if (p->subcmd == EC_PORT80_GET_INFO) {
		rsp->get_info.writes = writes;
		rsp->get_info.history_size = ARRAY_SIZE(history);
		args->response_size = sizeof(rsp->get_info);
		return EC_RES_SUCCESS;
	} else if (p->subcmd == EC_PORT80_READ_BUFFER) {
		/* do not allow bad offset or size */
		if (offset >= ARRAY_SIZE(history) || entries == 0 ||
			entries > args->response_max)
			return EC_RES_INVALID_PARAM;

		for (i = 0; i < entries; i++) {
			uint16_t e = history[(i + offset) %
				ARRAY_SIZE(history)];
			rsp->data.codes[i] = e;
		}

		args->response_size = entries*sizeof(uint16_t);
		return EC_RES_SUCCESS;
	}

	return EC_RES_INVALID_PARAM;
}
DECLARE_HOST_COMMAND(EC_CMD_PORT80_READ,
		port80_command_read,
		EC_VER_MASK(0) | EC_VER_MASK(1));

static void port80_log_resume(void)
{
	/* Store port 80 event so we know where resume happened */
	port_80_write(PORT_80_EVENT_RESUME);
}
DECLARE_HOOK(HOOK_CHIPSET_RESUME, port80_log_resume, HOOK_PRIO_DEFAULT);
