/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Host command module for Chrome EC */

#include "common.h"
#include "console.h"
#include "ec_commands.h"
#include "host_command.h"
#include "link_defs.h"
#include "lpc.h"
#include "system.h"
#include "task.h"
#include "timer.h"
#include "util.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_SYSTEM, outstr)
#define CPRINTF(format, args...) cprintf(CC_SYSTEM, format, ## args)

#define TASK_EVENT_CMD_PENDING TASK_EVENT_CUSTOM(1)

static int pending_cmd;

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

void host_command_received(int command)
{
	/* TODO: should warn if we already think we're in a command */

	/*
	 * If this is the reboot command, reboot immediately.  This gives the
	 * host processor a way to unwedge the EC even if it's busy with some
	 * other command.
	 */
	if (command == EC_CMD_REBOOT) {
		system_reset(1);
		/* Reset should never return; if it does, post an error */
		host_send_response(EC_RES_ERROR, NULL, 0);
		return;
	}

	/* Save the command */
	pending_cmd = command;

	/* Wake up the task to handle the command */
	task_set_event(TASK_ID_HOSTCMD, TASK_EVENT_CMD_PENDING, 0);
}

static int host_command_proto_version(struct host_cmd_handler_args *args)
{
	struct ec_response_proto_version *r =
		(struct ec_response_proto_version *)args->response;

	r->version = EC_PROTO_VERSION;
	args->response_size = sizeof(*r);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_PROTO_VERSION,
		     host_command_proto_version,
		     EC_VER_MASK(0));

static int host_command_hello(struct host_cmd_handler_args *args)
{
	const struct ec_params_hello *p =
		(const struct ec_params_hello *)args->params;
	struct ec_response_hello *r =
		(struct ec_response_hello *)args->response;
	uint32_t d = p->in_data;

	r->out_data = d + 0x01020304;
	args->response_size = sizeof(*r);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_HELLO,
		     host_command_hello,
		     EC_VER_MASK(0));

static int host_command_read_test(struct host_cmd_handler_args *args)
{
	const struct ec_params_read_test *p =
		(const struct ec_params_read_test *)args->params;
	struct ec_response_read_test *r =
		(struct ec_response_read_test *)args->response;

	int offset = p->offset;
	int size = p->size / sizeof(uint32_t);
	int i;

	if (size > ARRAY_SIZE(r->data))
		return EC_RES_ERROR;

	for (i = 0; i < size; i++)
		r->data[i] = offset + i;

	args->response_size = sizeof(*r);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_READ_TEST,
		     host_command_read_test,
		     EC_VER_MASK(0));

#ifndef CONFIG_LPC
/*
 * Host command to read memory map is not needed on LPC, because LPC can
 * directly map the data to the host's memory space.
 */
static int host_command_read_memmap(struct host_cmd_handler_args *args)
{
	const struct ec_params_read_memmap *p =
		(const struct ec_params_read_memmap *)args->params;

	/* Copy params out of data before we overwrite it with output */
	uint8_t offset = p->offset;
	uint8_t size = p->size;

	if (size > EC_PARAM_SIZE || offset > EC_MEMMAP_SIZE ||
	    offset + size > EC_MEMMAP_SIZE)
		return EC_RES_INVALID_PARAM;

	args->response = host_get_memmap(offset);
	args->response_size = size;

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_READ_MEMMAP,
		     host_command_read_memmap,
		     EC_VER_MASK(0));
#endif

/*
 * Find a command by command number.  Returns the command structure, or NULL if
 * no match found.
 */
static const struct host_command *find_host_command(int command)
{
	const struct host_command *cmd;

	for (cmd = __hcmds; cmd < __hcmds_end; cmd++) {
		if (command == cmd->command)
			return cmd;
	}

	return NULL;
}

enum ec_status host_command_process(int command, uint8_t *data,
				    int *response_size)
{
	const struct host_command *cmd = find_host_command(command);
	struct host_cmd_handler_args args;
	enum ec_status res;

	CPRINTF("[%T hostcmd 0x%02x]\n", command);

	if (!cmd)
		return EC_RES_INVALID_COMMAND;

	/* TODO: right now we assume the same data buffer for both params
	 * and response.  This isn't true for I2C/SPI; we should
	 * propagate args farther up the call chain. */
	args.command = command;
	args.version = 0;
	args.params = data;
	args.params_size = EC_PARAM_SIZE;
	args.response = data;
	args.response_size = 0;

	res = cmd->handler(&args);

	/* Copy response data if necessary */
	*response_size = args.response_size;
	if (args.response_size > EC_PARAM_SIZE)
		return EC_RES_INVALID_RESPONSE;
	else if (args.response_size && args.response != data)
		memcpy(data, args.response, args.response_size);

	return res;
}

/*****************************************************************************/
/* Initialization / task */

static int host_command_init(void)
{
	pending_cmd = -1;
	host_set_single_event(EC_HOST_EVENT_INTERFACE_READY);
	CPRINTF("[%T hostcmd init 0x%x]\n", host_get_events());

	return EC_SUCCESS;
}

void host_command_task(void)
{
	host_command_init();

	while (1) {
		/* wait for the next command event */
		int evt = task_wait_event(-1);
		/* process it */
		if (evt & TASK_EVENT_CMD_PENDING) {
			int size = 0;  /* Default to no response data */
			int res = host_command_process(pending_cmd,
						       host_get_buffer(),
						       &size);

			host_send_response(res, host_get_buffer(), size);
		}
	}
}
