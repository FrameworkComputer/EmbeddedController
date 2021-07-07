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

/**
 * See include/host_command.h for documentation.
 */
#define DECLARE_HOST_COMMAND(_command, _routine, _version_mask)            \
	STRUCT_SECTION_ITERABLE(host_command, _cros_hcmd_##_command) = {   \
		.command = _command,                                       \
		.handler = _routine,                                       \
		.version_mask = _version_mask,                             \
	}
#else /* !CONFIG_PLATFORM_EC_HOSTCMD */
#ifdef __clang__
#define DECLARE_HOST_COMMAND(command, routine, version_mask)
#else
#define DECLARE_HOST_COMMAND(command, routine, version_mask)         \
	enum ec_status (routine)(struct host_cmd_handler_args *args) \
		__attribute__((unused))
#endif /* __clang__ */
#endif /* CONFIG_PLATFORM_EC_HOSTCMD */
