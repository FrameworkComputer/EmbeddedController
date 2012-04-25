/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __MESSAGE_IF_H
#define __MESSAGE_IF_H

#include <common.h>

/* Command interface between EC and AP */

enum {
	MSG_TRAILER_BYTES	= 1,
	MSG_PROTO_BYTES		= MSG_TRAILER_BYTES,
};

/* The command codes that we understand */
enum message_cmd_t {
	/* EC control/status messages */
	CMDC_PROTO_VER	= 0x00,		/* Protocol version */
	CMDC_NOP,			/* No operation / ping */
	CMDC_ID,			/* Read EC ID */

	/* Functional messages */
	CMDC_KEY_STATE	= 0x20,		/* Read key state */
};


/**
 * Process a command received and return the response
 *
 * There is no time to compute a reply. The data should be ready
 * immediately. This function can be called in interrupt context.
 *
 * The format of a reply is a sequence of bytes:
 *
 * <msg bytes> <sum>
 *
 * The checksum is calculated as the sum of all message bytes
 *
 * @param cmd		Command to process (CMD_...)
 * @param buff		Pointer to buffer to store reponse
 * @param max_len	Maximum length of buffer
 * @return number of bytes in reply, 0 if none, -1 for unknown command
 */
int message_process_cmd(int cmd, uint8_t *buff, int max_len);

#endif
