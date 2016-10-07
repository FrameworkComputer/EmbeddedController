/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */


#include "config.h"
#include "board.h"
#include "extension.h"

void post_reset_command_handler(void *body,
				size_t cmd_size,
				size_t *response_size)
{
	*response_size = 1;
	((uint8_t *)body)[0] = 0;
	post_reboot_request();
}

DECLARE_EXTENSION_COMMAND(EXTENSION_POST_RESET, post_reset_command_handler);
