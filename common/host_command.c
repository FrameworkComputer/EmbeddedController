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

static struct host_cmd_handler_args *pending_args;

#ifndef CONFIG_LPC
static uint8_t host_memmap[EC_MEMMAP_SIZE];
#endif

static int hcdebug;  /* Enable extra host command debug output */

uint8_t *host_get_memmap(int offset)
{
#ifdef CONFIG_LPC
	return lpc_get_memmap_range() + offset;
#else
	return host_memmap + offset;
#endif
}

void host_send_response(struct host_cmd_handler_args *args)
{
	args->send_response(args);
}

void host_command_received(struct host_cmd_handler_args *args)
{
	/* TODO: should warn if we already think we're in a command */

	/*
	 * If this is the reboot command, reboot immediately.  This gives the
	 * host processor a way to unwedge the EC even if it's busy with some
	 * other command.
	 */
	if (args->command == EC_CMD_REBOOT) {
		system_reset(SYSTEM_RESET_HARD);
		/* Reset should never return; if it does, post an error */
		args->result = EC_RES_ERROR;
	}

	/* If the driver has signalled an error, send the response now */
	if (args->result) {
		host_send_response(args);
	} else {
		/* Save the command */
		pending_args = args;

		/* Wake up the task to handle the command */
		task_set_event(TASK_ID_HOSTCMD, TASK_EVENT_CMD_PENDING, 0);
	}
}

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

static int host_command_proto_version(struct host_cmd_handler_args *args)
{
	struct ec_response_proto_version *r = args->response;

	r->version = EC_PROTO_VERSION;
	args->response_size = sizeof(*r);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_PROTO_VERSION,
		     host_command_proto_version,
		     EC_VER_MASK(0));

static int host_command_hello(struct host_cmd_handler_args *args)
{
	const struct ec_params_hello *p = args->params;
	struct ec_response_hello *r = args->response;
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
	const struct ec_params_read_test *p = args->params;
	struct ec_response_read_test *r = args->response;

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
	const struct ec_params_read_memmap *p = args->params;

	/* Copy params out of data before we overwrite it with output */
	uint8_t offset = p->offset;
	uint8_t size = p->size;

	if (size > EC_MEMMAP_SIZE || offset > EC_MEMMAP_SIZE ||
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

static int host_command_get_cmd_versions(struct host_cmd_handler_args *args)
{
	const struct ec_params_get_cmd_versions *p = args->params;
	struct ec_response_get_cmd_versions *r = args->response;

	const struct host_command *cmd = find_host_command(p->cmd);

	if (!cmd)
		return EC_RES_INVALID_PARAM;

	r->version_mask = cmd->version_mask;

	args->response_size = sizeof(*r);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_GET_CMD_VERSIONS,
		     host_command_get_cmd_versions,
		     EC_VER_MASK(0));

enum ec_status host_command_process(struct host_cmd_handler_args *args)
{
	const struct host_command *cmd = find_host_command(args->command);
	enum ec_status rv;

	if (hcdebug && args->params_size)
		CPRINTF("[%T HC 0x%02x:%.*h]\n", args->command,
			args->params_size, args->params);
	else
		CPRINTF("[%T HC 0x%02x]\n", args->command);

	if (!cmd)
		rv = EC_RES_INVALID_COMMAND;
	else if (!(EC_VER_MASK(args->version) & cmd->version_mask))
		rv = EC_RES_INVALID_VERSION;
	else
		rv = cmd->handler(args);

	if (rv != EC_RES_SUCCESS) {
		CPRINTF("[%T HC err %d]\n", rv);
	} else if (hcdebug && args->response_size) {
		CPRINTF("[%T HC resp:%.*h]\n",
			args->response_size, args->response);
	}

	return rv;
}

/*****************************************************************************/
/* Initialization / task */

static int host_command_init(void)
{
	/* Initialize memory map ID area */
	host_get_memmap(EC_MEMMAP_ID)[0] = 'E';
	host_get_memmap(EC_MEMMAP_ID)[1] = 'C';
	*host_get_memmap(EC_MEMMAP_ID_VERSION) = 1;
	*host_get_memmap(EC_MEMMAP_EVENTS_VERSION) = 1;


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
		if ((evt & TASK_EVENT_CMD_PENDING) && pending_args) {
			pending_args->result =
					host_command_process(pending_args);
			host_send_response(pending_args);
		}
	}
}

/*****************************************************************************/
/* Console commands*/

static int parse_byte(char *b, uint8_t *out)
{
	int i;
	*out = 0;
	for (i = 0; i < 2; ++i) {
		*out *= 16;
		if (*b >= '0' && *b <= '9')
			*out += *b - '0';
		else if (*b >= 'a' && *b <= 'f')
			*out += *b - 'a' + 10;
		else if (*b >= 'A' && *b <= 'F')
			*out += *b - 'A' + 10;
		else
			return EC_ERROR_INVAL;
		++b;
	}
	return EC_SUCCESS;
}

static int parse_params(char *s, uint8_t *params)
{
	int len = 0;

	while (*s) {
		if (parse_byte(s, params))
			return -1;
		s += 2;
		params++;
		len++;
	}
	return len;
}

static int command_host_command(int argc, char **argv)
{
	struct host_cmd_handler_args args;
	uint8_t cmd_params[EC_HOST_PARAM_SIZE];
	enum ec_status res;
	char *e;
	int rv;

	/* Assume no version or params unless proven otherwise */
	args.version = 0;
	args.params_size = 0;
	args.params = cmd_params;

	if (argc < 2)
		return EC_ERROR_PARAM_COUNT;

	args.command = strtoi(argv[1], &e, 0);
	if (*e)
		return EC_ERROR_PARAM1;

	if (argc > 2) {
		args.version = strtoi(argv[2], &e, 0);
		if (*e)
			return EC_ERROR_PARAM2;
	}

	if (argc > 3) {
		rv = parse_params(argv[3], cmd_params);
		if (rv < 0)
			return EC_ERROR_PARAM3;
		args.params_size = rv;
	}

	args.response = cmd_params;
	args.response_max = EC_HOST_PARAM_SIZE;
	args.response_size = 0;

	res = host_command_process(&args);

	if (res != EC_RES_SUCCESS)
		ccprintf("Command returned %d\n", res);
	else if (args.response_size)
		ccprintf("Response: %.*h\n", args.response_size, cmd_params);
	else
		ccprintf("Command succeeded; no response.\n");

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(hostcmd, command_host_command,
			"cmd ver param",
			"Fake host command",
			NULL);

static int command_hcdebug(int argc, char **argv)
{
	if (argc > 1) {
		if (!strcasecmp(argv[1], "on"))
			hcdebug = 1;
		else if (!strcasecmp(argv[1], "off"))
			hcdebug = 0;
		else
			return EC_ERROR_PARAM1;
	}

	ccprintf("Host command debug is %s\n", hcdebug ? "on" : "off");
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(hcdebug, command_hcdebug,
			"hcdebug [on | off]",
			"Toggle extra host command debug output",
			NULL);
