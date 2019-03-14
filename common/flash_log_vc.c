/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "console.h"
#include "extension.h"
#include "flash_log.h"
#include "util.h"

static enum vendor_cmd_rc vc_pop_log_entry(enum vendor_cmd_cc code, void *buf,
					   size_t input_size,
					   size_t *response_size)
{
	uint32_t prev_timestamp;
	int byte_size;

	*response_size = 0; /* In case there is an error. */

	if (input_size != sizeof(uint32_t))
		return VENDOR_RC_BOGUS_ARGS;

	memcpy(&prev_timestamp, buf, sizeof(prev_timestamp));

	byte_size = flash_log_dequeue_event(
		prev_timestamp, buf,
		FLASH_LOG_ENTRY_SIZE(MAX_FLASH_LOG_PAYLOAD_SIZE));

	if (byte_size >= 0) {
		*response_size = byte_size;
		return VENDOR_RC_SUCCESS;
	}

	/* Negative value should fit into a byte. */
	*response_size = 1;
	((uint8_t *)buf)[0] = -byte_size;

	return VENDOR_RC_ERR;
}
DECLARE_VENDOR_COMMAND(VENDOR_CC_POP_LOG_ENTRY, vc_pop_log_entry);
