/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Host command module for Chrome EC */

#include "console.h"
#include "host_command.h"
#include "link_defs.h"
#include "lpc.h"
#include "ec_commands.h"
#include "system.h"
#include "task.h"
#include "timer.h"
#include "util.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_SYSTEM, outstr)
#define CPRINTF(format, args...) cprintf(CC_SYSTEM, format, ## args)

#define TASK_EVENT_SLOT(n) TASK_EVENT_CUSTOM(1 << n)

static int host_command[2];


void host_command_received(int slot, int command)
{
	/* TODO: should warn if we already think we're in a command */

	/* If this is the reboot command, reboot immediately.  This gives
	 * the host processor a way to unwedge the EC even if it's busy with
	 * some other command. */
	if (command == EC_CMD_REBOOT) {
		system_reset(1);
		/* Reset should never return; if it does, post an error */
		host_send_result(slot, EC_RES_ERROR);
		return;
	}

	/* Save the command */
	host_command[slot] = command;

	/* Wake up the task to handle the command for the slot */
	task_set_event(TASK_ID_HOSTCMD, TASK_EVENT_SLOT(slot), 0);
}

static int host_command_proto_version(uint8_t *data, int *resp_size)
{
	struct ec_response_proto_version *r =
		(struct ec_response_proto_version *)data;

	r->version = EC_PROTO_VERSION;

	*resp_size = sizeof(struct ec_response_proto_version);
	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_PROTO_VERSION,
		     host_command_proto_version);

static int host_command_hello(uint8_t *data, int *resp_size)
{
	struct ec_params_hello *p = (struct ec_params_hello *)data;
	struct ec_response_hello *r = (struct ec_response_hello *)data;
	uint32_t d = p->in_data;

	CPRINTF("[LPC Hello 0x%08x]\n", d);

#ifdef DELAY_HELLO_RESPONSE
	/* Pretend command takes a long time, so we can see the busy
	 * bit set on the host side. */
	/* TODO: remove in production.  Or maybe hello should take a
	 * param with how long the delay should be; that'd be more
	 * useful. */
	usleep(1000000);
#endif

	CPUTS("[LPC sending hello back]\n");

	r->out_data = d + 0x01020304;
	*resp_size = sizeof(struct ec_response_hello);
	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_HELLO, host_command_hello);


static int host_command_read_test(uint8_t *data, int *resp_size)
{
	struct ec_params_read_test *p = (struct ec_params_read_test *)data;
	struct ec_response_read_test *r =
			(struct ec_response_read_test *)data;

	int offset = p->offset;
	int size = p->size / sizeof(uint32_t);
	int i;

	if (size > ARRAY_SIZE(r->data))
		return EC_RES_ERROR;

	for (i = 0; i < size; i++)
		r->data[i] = offset + i;

	*resp_size = sizeof(struct ec_response_read_test);
	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_READ_TEST, host_command_read_test);


#ifdef CONFIG_LPC
/* ACPI query event handler.  Note that the returned value is NOT actually
 * an EC_RES enum; it's 0 if no event was pending, or the 1-based
 * index of the lowest bit which was set. */
static int host_command_acpi_query_event(uint8_t *data, int *resp_size)
{
	uint32_t events = lpc_get_host_events();
	int i;

	for (i = 0; i < 32; i++) {
		if (events & (1 << i)) {
			lpc_clear_host_events(1 << i);
			return i + 1;
		}
	}

	/* No events pending */
	return 0;
}
DECLARE_HOST_COMMAND(EC_CMD_ACPI_QUERY_EVENT,
		     host_command_acpi_query_event);
#endif


/* Finds a command by command number.  Returns the command structure, or NULL if
 * no match found. */
static const struct host_command *find_host_command(int command)
{
	const struct host_command *cmd;

	for (cmd = __hcmds; cmd < __hcmds_end; cmd++) {
		if (command == cmd->command)
			return cmd;
	}

	return NULL;
}


/* Handle a LPC command */
static void command_process(int slot)
{
	int command = host_command[slot];
	uint8_t *data = host_get_buffer(slot);
	const struct host_command *cmd = find_host_command(command);

	CPRINTF("[hostcmd%d 0x%02x]\n", slot, command);

	if (cmd) {
		int size = 0;
		int res = cmd->handler(data, &size);
		if ((res == EC_RES_SUCCESS) && size)
			host_send_response(slot, data, size);
		else
			host_send_result(slot, res);
	} else {
		host_send_result(slot, EC_RES_INVALID_COMMAND);
	}
}

/*****************************************************************************/
/* Initialization / task */

static int host_command_init(void)
{
	host_command[0] = host_command[1] = -1;

	return EC_SUCCESS;
}


void host_command_task(void)
{
	host_command_init();

	while (1) {
		/* wait for the next command event */
		int evt = task_wait_event(-1);
		/* process it */
		if (evt & TASK_EVENT_SLOT(0))
			command_process(0);
		if (evt & TASK_EVENT_SLOT(1))
			command_process(1);
	}
}
