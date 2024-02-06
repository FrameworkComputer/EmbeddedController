/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "battery.h"
#include "battery_fuel_gauge.h"
#include "charger.h"
#include "console.h"
#include "driver/charger/sm5803.h"
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
		rv = sm5803_is_acok(port, &acok);
		if ((rv == EC_SUCCESS) && acok)
			return 1;
	}

	return 0;
}

/*
 * Yavista does not have a GPIO indicating whether extpower is present,
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
		sm5803_hibernate(CHARGER_SECONDARY);
	sm5803_hibernate(CHARGER_PRIMARY);
	LOG_INF("Charger(s) hibernated");
	cflush();
}

__override int board_get_default_battery_type(void)
{
	int type = DEFAULT_BATTERY_TYPE;
	int cells;

	if (charger_get_battery_cells(CHARGER_PRIMARY, &cells) == EC_SUCCESS) {
		if (cells == 3)
			type = DEFAULT_BATTERY_TYPE_3S;
		if (cells != 2 && cells != 3)
			LOG_ERR("Unexpected number of cells");
	} else {
		LOG_ERR("Failed to get default battery type");
	}

	return type;
}
