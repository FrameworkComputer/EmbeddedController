/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "battery.h"
#include "battery_fuel_gauge.h"
#include "charger.h"
#include "charger/isl923x_public.h"
#include "console.h"
#include "driver/charger/isl923x.h"
#include "extpower.h"
#include "usb_pd.h"

#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(nissa, CONFIG_NISSA_LOG_LEVEL);

int extpower_is_present(void)
{
	int port;
	int rv;
	bool acok;

	for (port = 0; port < board_get_usb_pd_port_count(); port++) {
		rv = raa489000_is_acok(port, &acok);
		if ((rv == EC_SUCCESS) && acok)
			return 1;
	}

	return 0;
}

/*
 * Nivviks does not have a GPIO indicating whether extpower is present,
 * so detect using the charger(s).
 */
__override void board_check_extpower(void)
{
	static int last_extpower_present;
	int extpower_present = extpower_is_present();

	if (last_extpower_present ^ extpower_present)
		extpower_handle_update(extpower_present);

	last_extpower_present = extpower_present;
}

__override void board_hibernate(void)
{
	/* Shut down the chargers */
	if (board_get_usb_pd_port_count() == 2)
		raa489000_hibernate(CHARGER_SECONDARY, true);
	raa489000_hibernate(CHARGER_PRIMARY, true);
	LOG_INF("Charger(s) hibernated");
	cflush();
}

static int get_battery_cells(void)
{
	int val, battery_cells;

	if (i2c_read16(chg_chips[0].i2c_port, chg_chips[0].i2c_addr_flags,
		       ISL9238_REG_INFO2, &val) == EC_SUCCESS) {
		/* PROG resister read out. Number of battery cells [4:0] */
		val = val & 0x001f;
	}

	if (val == 0 || val >= 0x18)
		battery_cells = 1;
	else if (val >= 0x01 && val <= 0x08)
		battery_cells = 2;
	else if (val >= 0x09 && val <= 0x10)
		battery_cells = 3;
	else
		battery_cells = 4;

	return battery_cells;
}

__override int board_get_default_battery_type(void)
{
	int cells;
	int type = DEFAULT_BATTERY_TYPE;

	cells = get_battery_cells();
	LOG_INF("Get battery cells: %d", cells);

	if (cells == 3)
		type = DEFAULT_BATTERY_TYPE_3S;

	return type;
}
