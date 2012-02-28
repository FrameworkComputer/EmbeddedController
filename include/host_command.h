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
	/* Handler for the command; data points to parameters/response. */
	enum lpc_status (*handler)(uint8_t *data);
};


/* Called by LPC module when a command is written to one of the
   command slots (0=kernel, 1=user). */
void host_command_received(int slot, int command);

/* Register a host command handler */
#define DECLARE_HOST_COMMAND(command, routine)				\
	const struct host_command __host_cmd_##command			\
	__attribute__((section(".rodata.hcmds")))			\
	     = {command, routine}

#endif  /* __CROS_EC_HOST_COMMAND_H */
