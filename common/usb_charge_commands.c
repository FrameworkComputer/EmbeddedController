/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* USB charging control commands for Chrome EC */

#include "common.h"
#include "console.h"
#include "host_command.h"
#include "usb_charge.h"
#include "util.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_USBCHARGE, outstr)
#define CPRINTF(format, args...) cprintf(CC_USBCHARGE, format, ## args)

static int usb_charge_command_set_mode(struct host_cmd_handler_args *args)
{
	const struct ec_params_usb_charge_set_mode *p = args->params;
	int rv;

	CPRINTF("[%T USB setting port %d to mode %d]\n",
		p->usb_port_id, p->mode);

	rv = usb_charge_set_mode(p->usb_port_id, p->mode);

	if (rv != EC_SUCCESS)
		return EC_RES_ERROR;

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_USB_CHARGE_SET_MODE,
		     usb_charge_command_set_mode,
		     EC_VER_MASK(0));
