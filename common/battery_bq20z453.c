/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Smart battery driver for BQ20Z453.
 */

#include "host_command.h"
#include "smart_battery.h"
#include "timer.h"

#define PARAM_CUT_OFF 0x0010

int battery_command_cut_off(struct host_cmd_handler_args *args)
{
	/*
	 * TODO: Since this is a host command, the i2c bus is claimed by host.
	 *       Thus, we would send back the response in advanced so that
	 *       the host can release the bus and then EC can send command to
	 *       battery.
	 *
	 *       Refactoring this via task is a way. However, it is wasteful.
	 *       Need a light-weight solution.
	 */
	args->result = EC_RES_SUCCESS;
	host_send_response(args);

	/* This function would try to claim i2c and then send to battery. */
	sb_write(SB_MANUFACTURER_ACCESS, PARAM_CUT_OFF);

	return EC_RES_SUCCESS;
	/*
	 * Not sure if there is a side-effect since this could send result
	 * back to host TWICE.
	 */
}
DECLARE_HOST_COMMAND(EC_CMD_BATTERY_CUT_OFF, battery_command_cut_off,
		     EC_VER_MASK(0));
