/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "byteorder.h"
#include "console.h"
#include "extension.h"
#include "link_defs.h"
#include "util.h"

#define CPRINTS(format, args...) cprints(CC_EXTENSION, format, ## args)

uint32_t extension_route_command(uint16_t command_code,
				 void *buffer,
				 size_t in_size,
				 size_t *out_size,
				 uint32_t flags)
{
	struct extension_command *cmd_p;
	struct extension_command *end_p;
	const char *why_ignore = "not found";

#ifdef DEBUG_EXTENSION
	CPRINTS("%s(%d,%s)", __func__, command_code,
		flags & VENDOR_CMD_FROM_USB ? "USB" : "AP");
#endif

	/* Filter commands from USB */
	if (flags & VENDOR_CMD_FROM_USB) {
		switch (command_code) {
#ifdef CR50_DEV
		case VENDOR_CC_IMMEDIATE_RESET:
		case VENDOR_CC_INVALIDATE_INACTIVE_RW:
		case VENDOR_CC_SET_BOARD_ID:
#endif /* defined(CR50_DEV) */
		case EXTENSION_POST_RESET: /* Always need to reset. */
		case VENDOR_CC_GET_BOARD_ID:
		case VENDOR_CC_SPI_HASH:   /* Requires physical presence. */
		case VENDOR_CC_TURN_UPDATE_ON:
			break;
		default:
			/* Otherwise, we don't allow this command. */
			why_ignore = "usb";
			goto ignore_cmd;
		}
	}

#ifdef CONFIG_BOARD_ID_SUPPORT
	/*
	 * If board ID is mismatched, allow only the commands needed to upgrade
	 * Cr50 firmware.
	 */
	if (board_id_is_mismatched()) {
		switch (command_code) {
		case EXTENSION_FW_UPGRADE:
		case VENDOR_CC_REPORT_TPM_STATE:
		case VENDOR_CC_TURN_UPDATE_ON:
		case EXTENSION_POST_RESET:
			break;
		default:
			why_ignore = "BoardID mismatch";
			goto ignore_cmd;
		}
	}
#endif

	/* Find the command handler */
	cmd_p = (struct extension_command *)&__extension_cmds;
	end_p = (struct extension_command *)&__extension_cmds_end;
	while (cmd_p != end_p) {
		if (cmd_p->command_code == command_code)
			return cmd_p->handler(command_code, buffer,
					      in_size, out_size);
		cmd_p++;
	}

ignore_cmd:
	/* Command not found or not allowed */
	CPRINTS("%s: ignore %d: %s", __func__, command_code, why_ignore);
	*out_size = 0;
	return VENDOR_RC_NO_SUCH_COMMAND;
}
