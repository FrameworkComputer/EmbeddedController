/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "host_command.h"

static struct zshim_host_command_node *host_command_head;

void zshim_setup_host_command(
	int command,
	enum ec_status (*routine)(struct host_cmd_handler_args *args),
	int version_mask, struct zshim_host_command_node *entry)
{
	struct zshim_host_command_node **loc = &host_command_head;

	/* Setup the entry */
	entry->cmd->handler = routine;
	entry->cmd->command = command;
	entry->cmd->version_mask = version_mask;
	entry->next = *loc;

	/* Insert the entry */
	*loc = entry;
}

struct host_command *zephyr_find_host_command(int command)
{
	struct zshim_host_command_node *p;

	for (p = host_command_head; p != NULL; p = p->next) {
		if (p->cmd->command == command)
			return p->cmd;
	}

	return NULL;
}
