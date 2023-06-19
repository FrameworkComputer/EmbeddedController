/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

int battery_is_present(void)
{
	return 1;
}

int board_set_active_charge_port(int port)
{
	return 0;
}

int pd_set_power_supply_ready(int port)
{
	return 0;
}

void pd_power_supply_reset(int port)
{
}

int board_is_sourcing_vbus(int port)
{
	return 0;
}

int pd_check_vconn_swap(int port)
{
	return 0;
}

int extpower_is_present(void)
{
	return 0;
}

int lid_is_open(void)
{
	return 0;
}
