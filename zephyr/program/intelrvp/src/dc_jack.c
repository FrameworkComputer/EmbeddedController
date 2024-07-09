/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* DC Jack configuration */

#include "gpio.h"
#include "gpio/gpio_int.h"
#include "hooks.h"
#include "intelrvp.h"
#include "tcpm/tcpci.h"

#include <zephyr/init.h>

static struct k_work dc_jack_handle;

bool is_typec_port(int port)
{
	return !(port == DEDICATED_CHARGE_PORT || port == CHARGE_PORT_NONE);
}

int board_is_dc_jack_present(void)
{
	return gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(std_adp_prsnt));
}

static void board_dc_jack_handler(struct k_work *dc_jack_work)
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
	k_work_submit(&dc_jack_handle);
}

static void board_charge_init(void)
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

	k_work_init(&dc_jack_handle, board_dc_jack_handler);

	/* Handler not deffered during Board charge initialization */
	board_dc_jack_handler(NULL);
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_dc_jack_present));
}
#ifdef CONFIG_USB_PDC_POWER_MGMT
static int board_charge_sys_init(void)
{
	board_charge_init();
	return 0;
}
SYS_INIT(board_charge_sys_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
#else
DECLARE_HOOK(HOOK_INIT, board_charge_init, HOOK_PRIO_POST_CHARGE_MANAGER);
#endif
