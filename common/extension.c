/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "byteorder.h"
#include "console.h"
#include "extension.h"
#include "link_defs.h"

#define CPRINTF(format, args...) cprintf(CC_EXTENSION, format, ## args)

static uint32_t extension_route_command(uint16_t command_code,
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

uint32_t usb_extension_route_command(uint16_t command_code,
				     void *buffer,
				     size_t in_size,
				     size_t *out_size)
{
	int is_allowed = 0;

	switch (command_code) {
#ifdef CR50_DEV
	case VENDOR_CC_IMMEDIATE_RESET:
	case VENDOR_CC_TURN_UPDATE_ON:
#endif /* defined(CR50_DEV) */
	case EXTENSION_POST_RESET: /* Always need to be able to reset. */
		is_allowed = 1;
		break;

	default:
		break;
	}

	if (is_allowed)
		return extension_route_command(command_code, buffer, in_size,
					       out_size);

	/* Otherwise, we don't allow this command. */
	CPRINTF("%s: ignoring vendor cmd %d\n", __func__, command_code);
	*out_size = 0;
	return VENDOR_RC_NO_SUCH_COMMAND;
}

uint32_t tpm_extension_route_command(uint16_t command_code,
				     void *buffer,
				     size_t in_size,
				     size_t *out_size)
{
	/*
	 * TODO(aaboagye): Determine what commands (if any) should be filtered.
	 */
	return extension_route_command(command_code, buffer, in_size, out_size);
}
