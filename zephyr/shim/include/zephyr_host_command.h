/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#if !defined(__CROS_EC_HOST_COMMAND_H) || \
	defined(__CROS_EC_ZEPHYR_HOST_COMMAND_H)
#error "This file must only be included from host_command.h. " \
	"Include host_command.h directly"
#endif
#define __CROS_EC_ZEPHYR_HOST_COMMAND_H

#include <zephyr/init.h>
#include <stdbool.h>

/* Initializes and runs the host command handler loop.  */
void host_command_task(void *u);

/* Takes over the main thread and runs the host command loop. */
void host_command_main(void);

/* True if running in the main thread. */
bool in_host_command_main(void);

#ifdef CONFIG_PLATFORM_EC_HOSTCMD

/**
 * See include/host_command.h for documentation.
 */
#define DECLARE_HOST_COMMAND(_command, _routine, _version_mask)          \
	STRUCT_SECTION_ITERABLE(host_command, _cros_hcmd_##_command) = { \
		.command = _command,                                     \
		.handler = _routine,                                     \
		.version_mask = _version_mask,                           \
	}
#else /* !CONFIG_PLATFORM_EC_HOSTCMD */

/*
 * Create a global var to reference the host command. The linker should remove
 * it since it is never referenced.
 */
#define DECLARE_HOST_COMMAND(command, routine, version_mask) \
	int __remove_##command = ((int)(routine))

#endif /* CONFIG_PLATFORM_EC_HOSTCMD */
