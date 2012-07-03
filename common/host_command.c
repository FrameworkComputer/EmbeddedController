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

#ifndef CONFIG_LPC
static uint8_t host_memmap[EC_MEMMAP_SIZE];
#endif

uint8_t *host_get_memmap(int offset)
{
#ifdef CONFIG_LPC
	return lpc_get_memmap_range() + offset;
#else
	return host_memmap + offset;
#endif
}

void host_command_received(int slot, int command)
{
	/* TODO: should warn if we already think we're in a command */

	/* If this is the reboot command, reboot immediately.  This gives
	 * the host processor a way to unwedge the EC even if it's busy with
	 * some other command. */
	if (command == EC_CMD_REBOOT) {
		system_reset(1);
		/* Reset should never return; if it does, post an error */
		host_send_response(slot, EC_RES_ERROR, NULL, 0);
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
DECLARE_HOST_COMMAND(EC_CMD_PROTO_VERSION, host_command_proto_version);


static int host_command_hello(uint8_t *data, int *resp_size)
{
	struct ec_params_hello *p = (struct ec_params_hello *)data;
	struct ec_response_hello *r = (struct ec_response_hello *)data;
	uint32_t d = p->in_data;

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

#ifndef CONFIG_LPC
/*
 * Host command to read memory map is not needed on LPC, because LPC can
 * directly map the data to the host's memory space.
 */
static int host_command_read_memmap(uint8_t *data, int *resp_size)
{
	struct ec_params_read_memmap *p = (struct ec_params_read_memmap *)data;
	struct ec_response_read_memmap *r =
			(struct ec_response_read_memmap *)data;

	/* Copy params out of data before we overwrite it with output */
	uint8_t offset = p->offset;
	uint8_t size = p->size;

	if (size > sizeof(r->data) || offset > EC_MEMMAP_SIZE ||
	    offset + size > EC_MEMMAP_SIZE)
		return EC_RES_INVALID_PARAM;

	memcpy(r->data, host_get_memmap(offset), size);

	*resp_size = size;
	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_READ_MEMMAP, host_command_read_memmap);
#endif

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
DECLARE_HOST_COMMAND(EC_CMD_ACPI_QUERY_EVENT, host_command_acpi_query_event);
#endif  /* CONFIG_LPC */


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


enum ec_status host_command_process(int slot, int command, uint8_t *data,
				    int *response_size)
{
	const struct host_command *cmd = find_host_command(command);
	enum ec_status res = EC_RES_INVALID_COMMAND;

	CPRINTF("[hostcmd%d 0x%02x]\n", slot, command);

	*response_size = 0;
	if (cmd)
		res = cmd->handler(data, response_size);

	return res;
}

/* Handle a host command */
static void command_process(int slot)
{
	int size;
	int res;

	CPRINTF("[hostcmd%d 0x%02x]\n", slot, host_command[slot]);

	res = host_command_process(slot, host_command[slot],
				   host_get_buffer(slot), &size);

	host_send_response(slot, res, host_get_buffer(slot), size);
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
