/* Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Host command module for Chrome EC */

#include "config.h"
#include "console.h"
#include "flash_commands.h"
#include "host_command.h"
#include "lpc.h"
#include "lpc_commands.h"
#include "system.h"
#include "task.h"
#include "timer.h"
#include "uart.h"
#include "registers.h"
#include "util.h"

static int host_command = 0;
static uint8_t *host_data;

/*****************************************************************************/
/* Host commands */

void host_command_received(int command)
{
	/* TODO: should warn if we already think we're in a command */

	/* If this is the reboot command, reboot immediately.  This gives
	 * the host processor a way to unwedge the EC even if it's busy with
	 * some other command. */
	if (command == EC_LPC_COMMAND_REBOOT) {
		system_reset(1);
		/* Reset should never return; if it does, post an error */
		lpc_send_host_response(EC_LPC_STATUS_ERROR);
		return;
	}

	/* Save the command */
	host_command = command;

	/* Wake up the task to handle the command */
	task_send_msg(TASK_ID_HOSTCMD, TASK_ID_HOSTCMD, 0);
}


static enum lpc_status HostCommandHello(uint8_t *data)
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
	return EC_LPC_STATUS_SUCCESS;
}


static enum lpc_status HostCommandGetVersion(uint8_t *data)
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

	return EC_LPC_STATUS_SUCCESS;
}


static enum lpc_status HostCommandReadTest(uint8_t *data)
{
	struct lpc_params_read_test *p = (struct lpc_params_read_test *)data;
	struct lpc_response_read_test *r =
			(struct lpc_response_read_test *)data;

	int offset = p->offset;
	int size = p->size / sizeof(uint32_t);
	int i;

	if (size > ARRAY_SIZE(r->data))
		return EC_LPC_STATUS_ERROR;

	for (i = 0; i < size; i++)
		r->data[i] = offset + i;

	return EC_LPC_STATUS_SUCCESS;
}


/* handle a LPC command */
static void command_process(void)
{
	uart_printf("[hostcmd 0x%02x]\n", host_command);

	/* TODO: might be smaller to make this a table, once we get a bunch
	 * of commands. */
	switch (host_command) {
	case EC_LPC_COMMAND_HELLO:
		lpc_send_host_response(HostCommandHello(host_data));
		return;
	case EC_LPC_COMMAND_GET_VERSION:
		lpc_send_host_response(HostCommandGetVersion(host_data));
		return;
	case EC_LPC_COMMAND_READ_TEST:
		lpc_send_host_response(HostCommandReadTest(host_data));
		return;
	case EC_LPC_COMMAND_FLASH_INFO:
		lpc_send_host_response(flash_command_get_info(host_data));
		return;
	case EC_LPC_COMMAND_FLASH_READ:
		lpc_send_host_response(flash_command_read(host_data));
		return;
	case EC_LPC_COMMAND_FLASH_WRITE:
		lpc_send_host_response(flash_command_write(host_data));
		return;
	case EC_LPC_COMMAND_FLASH_ERASE:
		lpc_send_host_response(flash_command_erase(host_data));
		return;
	default:
		lpc_send_host_response(EC_LPC_STATUS_INVALID_COMMAND);
	}
}

/*****************************************************************************/
/* Console commands */

/* Command handler - prints EC version. */
static int command_version(int argc, char **argv)
{
	uart_printf("RO version:   %s\n",
		    system_get_version(SYSTEM_IMAGE_RO));
	uart_printf("RW-A version: %s\n",
		    system_get_version(SYSTEM_IMAGE_RW_A));
	uart_printf("RW-B version: %s\n",
		    system_get_version(SYSTEM_IMAGE_RW_B));
	return EC_SUCCESS;
}


static const struct console_command console_commands[] = {
        {"version", command_version},
};

static const struct console_group command_group = {
        "Host commands", console_commands, ARRAY_SIZE(console_commands)
};

/*****************************************************************************/
/* Initialization */

int host_command_init(void)
{
	host_data = (uint8_t *)lpc_get_host_range();

        console_register_commands(&command_group);
	return EC_SUCCESS;
}

void host_command_task(void)
{
	host_command_init();

	while (1) {
		/* wait for the next command message */
		task_wait_msg(-1);
		/* process it */
		command_process();
	}
}
