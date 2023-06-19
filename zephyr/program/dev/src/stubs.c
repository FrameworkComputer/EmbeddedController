/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Fakes for dev-posix which will be updated as work progresses
 */

#include "usb_pd.h"

int board_set_active_charge_port(int port)
{
	return 0;
}

int pd_check_vconn_swap(int port)
{
	return 0;
}

void pd_power_supply_reset(int port)
{
}

int pd_set_power_supply_ready(int port)
{
	return 0;
}
