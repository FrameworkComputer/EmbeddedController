/*
 * This handles incoming commands and provides responses.
 *
 * Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * EC <--> AP message handling.
 */

#include "board.h"
#include "message.h"
#include "keyboard_scan.h"
#include "util.h"

/* Our ID message - Matrix KeyBoard Protocol */
static const char proto_id[] = "Google Chrome MKBP v1";


/**
 * Get the response to a given command
 *
 * @param cmd		Command byte to respond to
 * @param buffp		Buffer to use for data, or it can be updated to
 *			point to a new buffer
 * @param max_len	Number of bytes free at *buffp for the response.
 *			If the supplied buffer is used, this size must not
 *			be exceeded.
 * @return number of bytes in response (which is in *buffp), or -1 on error
 */
static int message_get_response(int cmd, uint8_t **buffp, int max_len)
{
	/*
	 * Invalid commands are ignored, just returning a stream of 0xff
	 * bytes.
	 */
	switch (cmd & MSG_CMD_MASK) {
	case CMDC_NOP:
		return 0;
	case CMDC_ID:
		*buffp = (char *)proto_id;
		return sizeof(proto_id) - 1;
	case CMDC_KEY_STATE:
		return keyboard_get_scan(buffp, max_len);
	default:
		return -1;
	}

	return 0;
}

int message_process_cmd(int cmd, uint8_t *out_msg, int max_len)
{
	uint8_t *msg;
	int msg_len;
	int need_copy;
	int sum = 0;
	int len;
	int i;

	msg = out_msg + MSG_HEADER_BYTES;
	msg_len = message_get_response(cmd, &msg, max_len - MSG_PROTO_BYTES);
	if (msg_len < 0)
		return msg_len;

	/*
	 * We add MSG_PROTO_BYTES bytes of overhead: truncate the reply
	 * if needed.
	 */
	if (msg_len + MSG_PROTO_BYTES > max_len)
		msg_len = max_len - MSG_PROTO_BYTES;
	len = msg_len + MSG_PROTO_BYTES;
	ASSERT(msg_len >= 0 && msg_len < 0xffff);
	need_copy = msg != out_msg + MSG_HEADER_BYTES;
	ASSERT(!need_copy ||
		msg + msg_len < out_msg ||
		msg > out_msg + sizeof(out_msg));

	out_msg[0] = MSG_HEADER;
	out_msg[1] = len & 0xff;
	out_msg[2] = (len >> 8) & 0xff;
	sum += MSG_HEADER + len + (len >> 8);

	for (i = 0; i < msg_len; i++) {
		if (need_copy)
			out_msg[i + 3] = msg[i];
		sum += msg[i];
	}
	out_msg[i + 3] = sum & 0xff;
	out_msg[i + 4] = MSG_PREAMBLE;

	return msg_len + MSG_PROTO_BYTES;
}
