/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Host command module for Chrome EC */

#ifndef __CROS_EC_HOST_COMMAND_H
#define __CROS_EC_HOST_COMMAND_H

#include "common.h"
#include "lpc_commands.h"

/* Host command */
struct host_command {
	/* Command code. */
	int command;
	/* Handler for the command; data points to parameters/response.
	 * returns negative error code if case of failure (using EC_LPC_STATUS
	 * codes). sets <response_size> if it returns a payload to the host. */
	int (*handler)(uint8_t *data, int *response_size);
};


/* Called by LPC module when a command is written to one of the
   command slots (0=kernel, 1=user). */
void host_command_received(int slot, int command);

/* Send errors or success result code to a host command,
 * without response data.
 * <slot> is 0 for kernel-originated commands,
 *           1 for usermode-originated commands.
 * <result> is the error code. */
void host_send_result(int slot, int result);

   // success results with response data
/* Send a successful result code along with response data to a host command.
 * <slot> is 0 for kernel-originated commands,
 *           1 for usermode-originated commands.
 * <data> is the buffer with the response payload.
 * <size> is the size of the response buffer. */
void host_send_response(int slot, const uint8_t *data, int size);

/* Return a pointer to the host command data buffer.  This buffer must
 * only be accessed between a notification to host_command_received()
 * and a subsequent call to lpc_SendHostResponse().  <slot> is 0 for
 * kernel-originated commands, 1 for usermode-originated commands. */
uint8_t *host_get_buffer(int slot);

/* Register a host command handler */
#define DECLARE_HOST_COMMAND(command, routine)				\
	const struct host_command __host_cmd_##command			\
	__attribute__((section(".rodata.hcmds")))			\
	     = {command, routine}

#endif  /* __CROS_EC_HOST_COMMAND_H */
