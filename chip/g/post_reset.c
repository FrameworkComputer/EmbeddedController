/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */


#include "config.h"
#include "board.h"
#include "console.h"
#include "endian.h"
#include "extension.h"
#include "hooks.h"
#include "system.h"
#include "util.h"

#define CPRINTS(format, args...) cprints(CC_SYSTEM, format, ## args)

static void post_reset_command_handler(void *body,
				size_t cmd_size,
				size_t *response_size)
{
	*response_size = 1;
	((uint8_t *)body)[0] = 0;
	post_reboot_request();
}
DECLARE_EXTENSION_COMMAND(EXTENSION_POST_RESET, post_reset_command_handler);

static void deferred_reset(void)
{
	system_reset(SYSTEM_RESET_MANUALLY_TRIGGERED | SYSTEM_RESET_HARD);
}
DECLARE_DEFERRED(deferred_reset);

#define MAX_REBOOT_TIMEOUT_MS 1000

static enum vendor_cmd_rc immediate_reset(enum vendor_cmd_cc code,
					  void *buf,
					  size_t input_size,
					  size_t *response_size)
{
	uint16_t timeout = 0;

	*response_size = 0;
	if (input_size) {
		if (input_size != sizeof(uint16_t)) {
			CPRINTS("%s: incorrect request size %d",
				__func__, input_size);
			return VENDOR_RC_BOGUS_ARGS;
		}

		/* Retrieve the requested timeout. */
		memcpy(&timeout, buf, sizeof(timeout));
		timeout = be16toh(timeout);

		if (timeout > MAX_REBOOT_TIMEOUT_MS) {
			CPRINTS("%s: incorrect timeout value %d",
				__func__, timeout);
			return VENDOR_RC_BOGUS_ARGS;
		}
	}

	CPRINTS("%s: rebooting on host's request in %d ms", __func__, timeout);
	cflush(); /* Let the console drain. */

	if (timeout)
		hook_call_deferred(&deferred_reset_data, timeout * MSEC);
	else
		deferred_reset();

	return VENDOR_RC_SUCCESS;
}
DECLARE_VENDOR_COMMAND(VENDOR_CC_IMMEDIATE_RESET, immediate_reset);
