/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "byteorder.h"
#include "console.h"
#include "extension.h"
#include "link_defs.h"

#define CPRINTF(format, args...) cprintf(CC_EXTENSION, format, ## args)

uint32_t extension_route_command(uint16_t command_code,
				 void *buffer,
				 size_t in_size,
				 size_t *out_size)
{
	struct extension_command *cmd_p;
	struct extension_command *end_p;

	cmd_p = (struct extension_command *)&__extension_cmds;
	end_p = (struct extension_command *)&__extension_cmds_end;

	while (cmd_p != end_p) {
		if (cmd_p->command_code == command_code)
			return cmd_p->handler(command_code, buffer,
					      in_size, out_size);
		cmd_p++;
	}

	CPRINTF("%s: handler %d not found\n", __func__, command_code);

	/* This covers the case of the handler not found. */
	*out_size = 0;
	return VENDOR_RC_NO_SUCH_COMMAND;
}
