/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "byteorder.h"
#include "console.h"
#include "extension.h"
#include "link_defs.h"
#include "util.h"

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

#ifdef CONFIG_BOARD_ID_SUPPORT
	if (board_id_is_mismatched()) {
		switch (command_code) {
		case EXTENSION_FW_UPGRADE:
		case VENDOR_CC_REPORT_TPM_STATE:
		case VENDOR_CC_TURN_UPDATE_ON:
		case EXTENSION_POST_RESET:
			break;
		default:
			CPRINTF("%s: ignoring command 0x%x "
				"due to board ID mismatch\n",
				__func__, command_code);
			return VENDOR_RC_NO_SUCH_COMMAND;
		}
	}
#endif
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

void usb_extension_route_command(uint16_t command_code,
				 void *buffer,
				 size_t in_size,
				 size_t *out_size)
{
	uint32_t rv;
	uint8_t *buf = buffer;  /* Cache it for easy pointer arithmetics. */
	size_t buf_size;

	switch (command_code) {
#ifdef CR50_DEV
	case VENDOR_CC_IMMEDIATE_RESET:
	case VENDOR_CC_INVALIDATE_INACTIVE_RW:
	case VENDOR_CC_SET_BOARD_ID:
#endif /* defined(CR50_DEV) */
	case EXTENSION_POST_RESET: /* Always need to be able to reset. */
	case VENDOR_CC_GET_BOARD_ID:
	case VENDOR_CC_SPI_HASH:   /* This will require physical presence. */
	case VENDOR_CC_TURN_UPDATE_ON:

		/*
		 * The return code normally put into the TPM response header
		 * is not present in the USB response. Vendor command return
		 * code is guaranteed to fit in a byte. Let's keep space for
		 * it in the front of the buffer.
		 */
		buf_size = *out_size - 1;
		rv = extension_route_command(command_code, buffer,
					     in_size, &buf_size);
		/*
		 * Copy actual response, if any, one byte up, to free room for
		 * the return code.
		 */
		if (buf_size)
			memmove(buf + 1, buf, buf_size);
		*out_size = buf_size + 1;
		break;

	default:
		/* Otherwise, we don't allow this command. */
		CPRINTF("%s: ignoring vendor cmd %d\n", __func__, command_code);
		*out_size = 1;
		rv = VENDOR_RC_NO_SUCH_COMMAND;
		break;
	}

	buf[0] = rv;  /* We care about LSB only. */
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
