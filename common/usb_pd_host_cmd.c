/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Host commands for USB-PD module.
 */

#include "ec_commands.h"
#include "host_command.h"
#include "usb_pd.h"

#ifdef HAS_TASK_HOSTCMD

static enum ec_status hc_pd_ports(struct host_cmd_handler_args *args)
{
	struct ec_response_usb_pd_ports *r = args->response;

	r->num_ports = board_get_usb_pd_port_count();
	args->response_size = sizeof(*r);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_USB_PD_PORTS,
		     hc_pd_ports,
		     EC_VER_MASK(0));

#endif /* HAS_TASK_HOSTCMD */
