/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* DC Jack configuration */

#include "gpio.h"
#include "gpio/gpio_int.h"
#include "intelrvp.h"
#include "tcpm/tcpci.h"

#include <zephyr/init.h>

bool is_typec_port(int port)
{
	return !(port == DEDICATED_CHARGE_PORT || port == CHARGE_PORT_NONE);
}

int board_is_dc_jack_present(void)
{
	return gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(std_adp_prsnt));
}

static void board_dc_jack_handle(void)
{
	struct charge_port_info charge_dc_jack;

	/* System is booted from DC Jack */
	if (board_is_dc_jack_present()) {
		charge_dc_jack.current =
			(CONFIG_PLATFORM_EC_PD_MAX_POWER_MW * 1000) /
			DC_JACK_MAX_VOLTAGE_MV;
		charge_dc_jack.voltage = DC_JACK_MAX_VOLTAGE_MV;
	} else {
		charge_dc_jack.current = 0;
		charge_dc_jack.voltage = USB_CHARGER_VOLTAGE_MV;
	}

	charge_manager_update_charge(CHARGE_SUPPLIER_DEDICATED,
				     DEDICATED_CHARGE_PORT, &charge_dc_jack);
}

void board_dc_jack_interrupt(enum gpio_signal signal)
{
	board_dc_jack_handle();
}

static int board_charge_init(void)
{
	int port, supplier;
	struct charge_port_info charge_init = {
		.current = 0,
		.voltage = USB_CHARGER_VOLTAGE_MV,
	};

	/* Initialize all charge suppliers to seed the charge manager */
	for (port = 0; port < CHARGE_PORT_COUNT; port++) {
		for (supplier = 0; supplier < CHARGE_SUPPLIER_COUNT;
		     supplier++) {
			charge_manager_update_charge(supplier, port,
						     &charge_init);
		}
	}

	board_dc_jack_handle();
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_dc_jack_present));

	return 0;
}
SYS_INIT(board_charge_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
