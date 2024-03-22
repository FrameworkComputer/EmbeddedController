/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "battery.h"
#include "charger.h"
#include "charger/isl923x_public.h"
#include "console.h"
#include "extpower.h"
#include "usb_pd.h"

#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(charger, LOG_LEVEL_INF);

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
 * Xivu does not have a GPIO indicating whether extpower is present,
 * so detect using the charger(s).
 */
__override void board_check_extpower(void)
{
	static int last_extpower_present;
	int extpower_present_p0 = 0;
	int extpower_present_p1 = 0;

	int extpower_present = extpower_is_present();

	if (last_extpower_present ^ extpower_present)
		extpower_handle_update(extpower_present);

	last_extpower_present = extpower_present;

	if (pd_is_connected(0))
		extpower_present_p0 = extpower_is_present();
	else if (pd_is_connected(1))
		extpower_present_p1 = extpower_is_present();

	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_acok_otg_c0),
			extpower_present_p0);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_acok_otg_c1),
			extpower_present_p1);
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
