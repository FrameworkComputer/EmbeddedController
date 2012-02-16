/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* USB charging control commands for Chrome EC */

#include "console.h"
#include "usb_charge.h"
#include "usb_charge_commands.h"
#include "lpc_commands.h"
#include "uart.h"
#include "util.h"


/*****************************************************************************/
/* Host commands */

enum lpc_status usb_charge_command_set_mode(uint8_t *data)
{
	struct lpc_params_usb_charge_set_mode *p =
			(struct lpc_params_usb_charge_set_mode *)data;
	int rv;

	uart_printf("[Setting USB port %d to mode %d]\n",
		    p->usb_port_id, p->mode);
	rv = usb_charge_set_mode(p->usb_port_id, p->mode);

	if (rv != EC_SUCCESS)
		return EC_LPC_STATUS_ERROR;

	return EC_LPC_STATUS_SUCCESS;
}
