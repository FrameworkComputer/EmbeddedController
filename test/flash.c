/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Console commands to trigger flash host commands */

#include "board.h"
#include "console.h"
#include "ec_commands.h"
#include "host_command.h"
#include "uart.h"
#include "util.h"

static int ro_image_size(int argc, char **argv)
{
	uart_printf("RO image size = 0x%x\n", CONFIG_SECTION_RO_SIZE);
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(rosize, ro_image_size,
			NULL,
			"Report size of RO image",
			NULL);
