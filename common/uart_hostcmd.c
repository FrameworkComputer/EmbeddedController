/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "config.h"
#include "common.h"
#include "ec_commands.h"
#include "host_command.h"
#include "uart.h"

static enum ec_status
host_command_console_snapshot(struct host_cmd_handler_args *args)
{
	return uart_console_read_buffer_init();
}
DECLARE_HOST_COMMAND(EC_CMD_CONSOLE_SNAPSHOT, host_command_console_snapshot,
		     EC_VER_MASK(0));

static enum ec_status
host_command_console_read(struct host_cmd_handler_args *args)
{
	if (args->version == 0) {
		/*
		 * Prior versions of this command only support reading from
		 * an entire snapshot, not just the output since the last
		 * snapshot.
		 */
		return uart_console_read_buffer(CONSOLE_READ_NEXT,
						(char *)args->response,
						args->response_max,
						&args->response_size);
	} else if (IS_ENABLED(CONFIG_CONSOLE_ENABLE_READ_V1) &&
		   args->version == 1) {
		const struct ec_params_console_read_v1 *p;

		/* Check the params to figure out where to start reading. */
		p = args->params;
		return uart_console_read_buffer(p->subcmd,
						(char *)args->response,
						args->response_max,
						&args->response_size);
	}
	return EC_RES_INVALID_PARAM;
}

#ifdef CONFIG_CONSOLE_ENABLE_READ_V1
#define READ_V1_MASK EC_VER_MASK(1)
#else
#define READ_V1_MASK 0
#endif

DECLARE_HOST_COMMAND(EC_CMD_CONSOLE_READ, host_command_console_read,
		     EC_VER_MASK(0) | READ_V1_MASK);
