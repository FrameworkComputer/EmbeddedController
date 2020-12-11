/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Shared USB-C policy for Zork boards */

#include "charge_manager.h"
#include "chipset.h"
#include "common.h"
#include "compile_time_macros.h"
#include "console.h"
#include "ec_commands.h"
#include "gpio.h"
#include "system.h"
#include "usb_mux.h"
#include "usb_pd.h"
#include "usbc_ppc.h"
#include "util.h"

int pd_check_vconn_swap(int port)
{
	/* TODO */
	return 0;
}

void pd_power_supply_reset(int port)
{
	/* TODO */
}

int pd_set_power_supply_ready(int port)
{
	/* TODO */
	return 0;
}

int board_vbus_source_enabled(int port)
{
	return ppc_is_sourcing_vbus(port);
}
