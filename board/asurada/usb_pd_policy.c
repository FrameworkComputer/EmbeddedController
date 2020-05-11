/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "usb_pd.h"

int pd_snk_is_vbus_provided(int port)
{
	return 0;
}

void pd_power_supply_reset(int port)
{
}

int pd_check_vconn_swap(int port)
{
	return 0;
}

int pd_set_power_supply_ready(int port)
{
	return 0;
}
