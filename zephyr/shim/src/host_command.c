/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr.h>

#include "host_command.h"
#include "task.h"

struct host_command *zephyr_find_host_command(int command)
{
	STRUCT_SECTION_FOREACH(host_command, cmd) {
		if (cmd->command == command)
			return cmd;
	}

	return NULL;
}

/* Pointer to the main thread, defined in kernel/init.c */
extern struct k_thread z_main_thread;

void host_command_main(void)
{
	k_thread_priority_set(&z_main_thread,
			      EC_TASK_PRIORITY(EC_TASK_HOSTCMD_PRIO));
	k_thread_name_set(&z_main_thread, "HOSTCMD");
	host_command_task(NULL);
}

bool in_host_command_main(void)
{
	return (k_current_get() == &z_main_thread);
}
