/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Host command module for Chrome EC */

#include "console.h"
#include "host_command.h"
#include "lpc.h"
#include "lpc_commands.h"
#include "system.h"
#include "task.h"
#include "timer.h"
#include "uart.h"
#include "util.h"

static int host_command[2];

/* Host commands are described in a special section */
extern const struct host_command __hcmds[];
extern const struct host_command __hcmds_end[];

/*****************************************************************************/
/* Host commands */

void host_command_received(int slot, int command)
{
	/* TODO: should warn if we already think we're in a command */

	/* If this is the reboot command, reboot immediately.  This gives
	 * the host processor a way to unwedge the EC even if it's busy with
	 * some other command. */
	if (command == EC_LPC_COMMAND_REBOOT) {
		system_reset(1);
		/* Reset should never return; if it does, post an error */
		lpc_send_host_response(slot, EC_LPC_RESULT_ERROR);
		return;
	}

	/* Save the command */
	host_command[slot] = command;

	/* Wake up the task to handle the command.  Use the slot as
	 * the task ID. */
	task_send_msg(TASK_ID_HOSTCMD, slot, 0);
}


static enum lpc_status host_command_hello(uint8_t *data)
{
	struct lpc_params_hello *p = (struct lpc_params_hello *)data;
	struct lpc_response_hello *r = (struct lpc_response_hello *)data;
	uint32_t d = p->in_data;

	uart_printf("[LPC Hello 0x%08x]\n", d);

#ifdef DELAY_HELLO_RESPONSE
	/* Pretend command takes a long time, so we can see the busy
	 * bit set on the host side. */
	/* TODO: remove in production.  Or maybe hello should take a
	 * param with how long the delay should be; that'd be more
	 * useful. */
	usleep(1000000);
#endif

	uart_puts("[LPC sending hello back]\n");

	r->out_data = d + 0x01020304;
	return EC_LPC_RESULT_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_LPC_COMMAND_HELLO, host_command_hello);


static enum lpc_status host_command_get_version(uint8_t *data)
{
	struct lpc_response_get_version *r =
			(struct lpc_response_get_version *)data;

	uart_printf("[LPC GetVersion]\n");

	strzcpy(r->version_string_ro, system_get_version(SYSTEM_IMAGE_RO),
		sizeof(r->version_string_ro));
	strzcpy(r->version_string_rw_a, system_get_version(SYSTEM_IMAGE_RW_A),
		sizeof(r->version_string_rw_a));
	strzcpy(r->version_string_rw_b, system_get_version(SYSTEM_IMAGE_RW_B),
		sizeof(r->version_string_rw_b));

	switch(system_get_image_copy()) {
	case SYSTEM_IMAGE_RO:
		r->current_image = EC_LPC_IMAGE_RO;
		break;
	case SYSTEM_IMAGE_RW_A:
		r->current_image = EC_LPC_IMAGE_RW_A;
		break;
	case SYSTEM_IMAGE_RW_B:
		r->current_image = EC_LPC_IMAGE_RW_B;
		break;
	default:
		r->current_image = EC_LPC_IMAGE_UNKNOWN;
		break;
	}

	return EC_LPC_RESULT_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_LPC_COMMAND_GET_VERSION, host_command_get_version);


static enum lpc_status host_command_read_test(uint8_t *data)
{
	struct lpc_params_read_test *p = (struct lpc_params_read_test *)data;
	struct lpc_response_read_test *r =
			(struct lpc_response_read_test *)data;

	int offset = p->offset;
	int size = p->size / sizeof(uint32_t);
	int i;

	if (size > ARRAY_SIZE(r->data))
		return EC_LPC_RESULT_ERROR;

	for (i = 0; i < size; i++)
		r->data[i] = offset + i;

	return EC_LPC_RESULT_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_LPC_COMMAND_READ_TEST, host_command_read_test);


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
	uint8_t *data = lpc_get_host_range(slot);
	const struct host_command *cmd = find_host_command(command);

	uart_printf("[hostcmd%d 0x%02x]\n", slot, command);

	if (cmd)
		lpc_send_host_response(slot, cmd->handler(data));
	else
		lpc_send_host_response(slot, EC_LPC_RESULT_INVALID_COMMAND);
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
		/* wait for the next command message */
		int m = task_wait_msg(-1);
		/* process it */
		/* TODO: use message flags to determine which slots */
		if (m & 0x01)
			command_process(0);
		if (m & 0x02)
			command_process(1);
	}
}
