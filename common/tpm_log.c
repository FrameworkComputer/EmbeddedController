/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "console.h"
#include "endian.h"
#include "extension.h"
#include "host_command.h"
#include "timer.h"
#include "tpm_log.h"
#include "tpm_vendor_cmds.h"
#include "usb_pd.h"
#include "util.h"

/*
 * TPM event logging uses the standard 'event_log_entry' as its storage,
 * with no additional payload bytes.
 */
#define TPM_EVENT_LOG_SIZE sizeof(struct event_log_entry)

void tpm_log_event(enum tpm_event type, uint16_t data)
{
	uint32_t timestamp = get_time().val >> EVENT_LOG_TIMESTAMP_SHIFT;

	log_add_event(type, 0, data, NULL, timestamp);
}

static enum vendor_cmd_rc vc_pop_log_entry(enum vendor_cmd_cc code,
					   void *buf,
					   size_t input_size,
					   size_t *response_size)
{
	struct event_log_entry *entry = buf;
	int byte_size = log_dequeue_event(entry);

	if (entry->type == EVENT_LOG_NO_ENTRY) {
		*response_size = 0;
		return VENDOR_RC_SUCCESS;
	}
	if (byte_size != TPM_EVENT_LOG_SIZE)
		return VENDOR_RC_INTERNAL_ERROR;

	entry->timestamp = htobe32(entry->timestamp);
	entry->data = htobe16(entry->data);
	*response_size = byte_size;

	return VENDOR_RC_SUCCESS;
}
DECLARE_VENDOR_COMMAND(VENDOR_CC_POP_LOG_ENTRY, vc_pop_log_entry);

#ifdef CONFIG_CMD_TPM_LOG
/* Store an entry in the TPM event log, for testing. */
int command_tpm_log(int argc, char **argv)
{
	enum tpm_event type = 0;
	uint16_t data = 0;
	char *e;

	if (argc >= 2) {
		type = strtoi(argv[1], &e, 10);
		if (*e)
			return EC_ERROR_PARAM1;
	}

	if (argc >= 3) {
		data = strtoi(argv[2], &e, 10);
		if (*e)
			return EC_ERROR_PARAM2;
	}

	tpm_log_event(type, data);
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(tpm_log,
			command_tpm_log,
			"<type> <data>",
			"Write an entry to TPM log");
#endif /* CONFIG_CMD_TPM_LOG */
