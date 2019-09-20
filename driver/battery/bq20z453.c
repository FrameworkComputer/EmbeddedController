/* Copyright 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Smart battery driver for BQ20Z453.
 */

#include "battery_smart.h"
#include "hooks.h"
#include "host_command.h"

#define PARAM_CUT_OFF 0x0010

static void cutoff(void)
{
	/* Claim i2c and send cutoff command to battery. */
	sb_write(SB_MANUFACTURER_ACCESS, PARAM_CUT_OFF);
}
DECLARE_DEFERRED(cutoff);

enum ec_status battery_command_cut_off(struct host_cmd_handler_args *args)
{
	/*
	 * Queue battery cutoff.  This must be deferred so we can send the
	 * response to the host first.  Some platforms (snow) share an I2C bus
	 * between the EC, AP, and battery, so we need the host to complete the
	 * transaction and release the I2C bus before we'll be abl eto send the
	 * cutoff command.
	 */
	hook_call_deferred(&cutoff_data, 1000);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_BATTERY_CUT_OFF, battery_command_cut_off,
		     EC_VER_MASK(0));
