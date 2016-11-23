/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */


#include "config.h"
#include "board.h"
#include "console.h"
#include "extension.h"
#include "system.h"

#define CPRINTS(format, args...) cprints(CC_SYSTEM, format, ## args)

void post_reset_command_handler(void *body,
				size_t cmd_size,
				size_t *response_size)
{
	*response_size = 1;
	((uint8_t *)body)[0] = 0;
	post_reboot_request();
}

DECLARE_EXTENSION_COMMAND(EXTENSION_POST_RESET, post_reset_command_handler);

static enum vendor_cmd_rc immediate_reset(enum vendor_cmd_cc code,
					  void *buf,
					  size_t input_size,
					  size_t *response_size)
{
	CPRINTS("%s: rebooting on host's request", __func__);
	cflush(); /* Let the console drain. */
	system_reset(SYSTEM_RESET_HARD);  /* This will never return. */

	/* Never reached. */
	return VENDOR_RC_SUCCESS;
}
DECLARE_VENDOR_COMMAND(VENDOR_CC_IMMEDIATE_RESET, immediate_reset);
