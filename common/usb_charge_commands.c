/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* USB charging control commands for Chrome EC */

#include "console.h"
#include "usb_charge.h"
#include "host_command.h"
#include "uart.h"
#include "util.h"


enum lpc_status usb_charge_command_set_mode(uint8_t *data)
{
	struct lpc_params_usb_charge_set_mode *p =
			(struct lpc_params_usb_charge_set_mode *)data;
	int rv;

	uart_printf("[Setting USB port %d to mode %d]\n",
		    p->usb_port_id, p->mode);
	rv = usb_charge_set_mode(p->usb_port_id, p->mode);

	if (rv != EC_SUCCESS)
		return EC_LPC_RESULT_ERROR;

	return EC_LPC_RESULT_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_LPC_COMMAND_USB_CHARGE_SET_MODE,
		     usb_charge_command_set_mode);
