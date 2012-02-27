/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Host command module for Chrome EC */

#include "config.h"
#include "console.h"
#include "flash_commands.h"
#include "host_command.h"
#include "pwm_commands.h"
#include "usb_charge_commands.h"
#include "lpc.h"
#include "lpc_commands.h"
#include "system.h"
#include "task.h"
#include "timer.h"
#include "uart.h"
#include "registers.h"
#include "util.h"

static int host_command[2];

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
		lpc_send_host_response(slot, EC_LPC_STATUS_ERROR);
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
	return EC_LPC_STATUS_SUCCESS;
}


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

	return EC_LPC_STATUS_SUCCESS;
}


static enum lpc_status host_command_read_test(uint8_t *data)
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
static void command_process(int slot)
{
	int command = host_command[slot];
	uint8_t *data = lpc_get_host_range(slot);

	uart_printf("[hostcmd%d 0x%02x]\n", slot, command);

	/* TODO: might be smaller to make this a table, once we get a bunch
	 * of commands. */
	switch (command) {
	case EC_LPC_COMMAND_HELLO:
		lpc_send_host_response(slot, host_command_hello(data));
		return;
	case EC_LPC_COMMAND_GET_VERSION:
		lpc_send_host_response(slot, host_command_get_version(data));
		return;
	case EC_LPC_COMMAND_READ_TEST:
		lpc_send_host_response(slot, host_command_read_test(data));
		return;
	case EC_LPC_COMMAND_FLASH_INFO:
		lpc_send_host_response(slot, flash_command_get_info(data));
		return;
	case EC_LPC_COMMAND_FLASH_READ:
		lpc_send_host_response(slot, flash_command_read(data));
		return;
	case EC_LPC_COMMAND_FLASH_WRITE:
		lpc_send_host_response(slot, flash_command_write(data));
		return;
	case EC_LPC_COMMAND_FLASH_ERASE:
		lpc_send_host_response(slot, flash_command_erase(data));
		return;
	case EC_LPC_COMMAND_FLASH_WP_ENABLE:
		lpc_send_host_response(slot, flash_command_wp_enable(data));
		return;
	case EC_LPC_COMMAND_FLASH_WP_GET_STATE:
		lpc_send_host_response(slot, flash_command_wp_get_state(data));
		return;
	case EC_LPC_COMMAND_FLASH_WP_SET_RANGE:
		lpc_send_host_response(slot, flash_command_wp_set_range(data));
		return;
	case EC_LPC_COMMAND_FLASH_WP_GET_RANGE:
		lpc_send_host_response(slot, flash_command_wp_get_range(data));
		return;
#ifdef SUPPORT_CHECKSUM
	case EC_LPC_COMMAND_FLASH_CHECKSUM:
		lpc_send_host_response(slot, flash_command_checksum(data));
		return;
#endif
	case EC_LPC_COMMAND_PWM_GET_FAN_RPM:
		lpc_send_host_response(slot, pwm_command_get_fan_rpm(data));
	        return;
	case EC_LPC_COMMAND_PWM_SET_FAN_TARGET_RPM:
	        lpc_send_host_response(slot,
				       pwm_command_set_fan_target_rpm(data));
	        return;
	case EC_LPC_COMMAND_PWM_GET_KEYBOARD_BACKLIGHT:
		lpc_send_host_response(slot,
		    pwm_command_get_keyboard_backlight(data));
	        return;
	case EC_LPC_COMMAND_PWM_SET_KEYBOARD_BACKLIGHT:
	        lpc_send_host_response(slot,
		    pwm_command_set_keyboard_backlight(data));
	        return;
	case EC_LPC_COMMAND_USB_CHARGE_SET_MODE:
		lpc_send_host_response(slot, usb_charge_command_set_mode(data));
		return;
	default:
		lpc_send_host_response(slot, EC_LPC_STATUS_INVALID_COMMAND);
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
DECLARE_CONSOLE_COMMAND(version, command_version);

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
