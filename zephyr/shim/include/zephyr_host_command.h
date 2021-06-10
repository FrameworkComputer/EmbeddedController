/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#if !defined(__CROS_EC_HOST_COMMAND_H) || \
	defined(__CROS_EC_ZEPHYR_HOST_COMMAND_H)
#error "This file must only be included from host_command.h. " \
	"Include host_command.h directly"
#endif
#define __CROS_EC_ZEPHYR_HOST_COMMAND_H

#include <init.h>

#ifdef CONFIG_PLATFORM_EC_HOSTCMD

/** Node in a list of host-command handlers */
struct zshim_host_command_node {
	struct host_command *cmd;
	struct zshim_host_command_node *next;
};

/**
 * Runtime helper for DECLARE_HOST_COMMAND setup data.
 *
 * @param routine	Handler for the host command
 * @param command	Command to handle (EC_CMD_...)
 * @param version_mask  Mask of supported versions; use EC_VER_MASK() to select
 *			a version
 */
void zshim_setup_host_command(
	int command,
	enum ec_status (*routine)(struct host_cmd_handler_args *args),
	int version_mask, struct zshim_host_command_node *entry);

/**
 * See include/host_command.h for documentation.
 */
#define DECLARE_HOST_COMMAND(command, routine, version_mask) \
	_DECLARE_HOST_COMMAND_1(command, routine, version_mask, __LINE__)
#define _DECLARE_HOST_COMMAND_1(command, routine, version_mask, line) \
	_DECLARE_HOST_COMMAND_2(command, routine, version_mask, line)
#define _DECLARE_HOST_COMMAND_2(command, routine, version_mask, line)      \
	static int _setup_host_command_##line(const struct device *unused) \
	{                                                                  \
		ARG_UNUSED(unused);                                        \
		static struct host_command cmd;                            \
		static struct zshim_host_command_node lst;                 \
		lst.cmd = &cmd;                                            \
		zshim_setup_host_command(command, routine, version_mask,   \
					 &lst);                            \
		return 0;                                                  \
	}                                                                  \
	SYS_INIT(_setup_host_command_##line, APPLICATION, 1)
#else /* !CONFIG_PLATFORM_EC_HOSTCMD */
#ifdef __clang__
#define DECLARE_HOST_COMMAND(command, routine, version_mask)
#else
#define DECLARE_HOST_COMMAND(command, routine, version_mask)         \
	enum ec_status (routine)(struct host_cmd_handler_args *args) \
		__attribute__((unused))
#endif /* __clang__ */
#endif /* CONFIG_PLATFORM_EC_HOSTCMD */
