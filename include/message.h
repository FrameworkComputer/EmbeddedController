/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __MESSAGE_IF_H
#define __MESSAGE_IF_H

#include <common.h>

/* Command interface between EC and AP */

enum {
	/* Mask to convert a command byte into a command */
	MSG_CMD_MASK	= 0x7f,

	/* The bytes which appear before the header in a message */
	MSG_PREAMBLE	= 0xff,

	/* The header byte, which follows the preamble */
	MSG_HEADER	= 0xec,

	MSG_HEADER_BYTES	= 3,
	MSG_TRAILER_BYTES	= 2,
	MSG_PROTO_BYTES		= MSG_HEADER_BYTES + MSG_TRAILER_BYTES,
};

/* The command codes that we understand */
enum message_cmd_t {
	CMDC_NOP,	/* No operation / ping */
	CMDC_ID,	/* Read EC ID */
	CMDC_KEY_STATE,	/* Read key state */
};


/**
 * Process a command received and return the response
 *
 * There is no time to compute a reply. The data should be ready
 * immediately. This function can be called in interrupt context.
 *
 * The format of a reply is a sequence of bytes:
 *
 * <hdr> <len_lo> <len_hi> <msg bytes> <sum> <preamble bytes>
 *
 * The hdr byte is just a tag to indicate that the real message follows. It
 * signals the end of any preamble required by the interface.
 *
 * The 16-bit length includes the length of the header, len bytes, message
 * and checksum, but not the final preamble byte.
 *
 * The checksum is calculated as the sum of the header, len byte and message.
 *
 * @param cmd		Command to process (CMD_...)
 * @param buff		Pointer to buffer to store reponse
 * @param max_len	Maximum length of buffer
 * @return number of bytes in reply, 0 if none, -1 for unknown command
 */
int message_process_cmd(int cmd, uint8_t *buff, int max_len);

#endif
