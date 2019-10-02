/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Intel-RVP family-specific configuration */

#include "charge_manager.h"
#include "charge_state_v2.h"
#include "console.h"
#include "hooks.h"
#include "tcpci.h"
#include "system.h"

#define CPRINTF(format, args...) cprintf(CC_USBPD, format, ## args)
#define CPRINTS(format, args...) cprints(CC_USBPD, format, ## args)

static inline int is_typec_port(int port)
{
	return !(port == DEDICATED_CHARGE_PORT || port == CHARGE_PORT_NONE);
}


int board_vbus_source_enabled(int port)
{
	int src_en = 0;

	/* Only Type-C ports can source VBUS */
	if (is_typec_port(port)) {
		src_en = gpio_get_level(tcpc_gpios[port].src.pin);

		src_en = tcpc_gpios[port].src.pin_pol ? src_en : !src_en;
	}

	return src_en;
}

void board_set_vbus_source_current_limit(int port, enum tcpc_rp_value rp)
{
	int ilim_en;

	/* Only Type-C ports can source VBUS */
	if (is_typec_port(port)) {
		/* Enable SRC ILIM if rp is MAX single source current */
		ilim_en = (rp == CONFIG_USB_PD_MAX_SINGLE_SOURCE_CURRENT &&
			board_vbus_source_enabled(port));

		gpio_set_level(tcpc_gpios[port].src_ilim.pin,
				tcpc_gpios[port].src_ilim.pin_pol ?
				ilim_en : !ilim_en);
	}
}

void board_charging_enable(int port, int enable)
{
	gpio_set_level(tcpc_gpios[port].snk.pin,
		tcpc_gpios[port].snk.pin_pol ? enable : !enable);

}

void board_vbus_enable(int port, int enable)
{
	gpio_set_level(tcpc_gpios[port].src.pin,
		tcpc_gpios[port].src.pin_pol ? enable : !enable);
}

int pd_snk_is_vbus_provided(int port)
{
	int vbus_intr;

	if (port == DEDICATED_CHARGE_PORT)
		return 1;

	vbus_intr = gpio_get_level(tcpc_gpios[port].vbus.pin);

	return tcpc_gpios[port].vbus.pin_pol ? vbus_intr : !vbus_intr;
}

void tcpc_alert_event(enum gpio_signal signal)
{
#ifdef HAS_TASK_PDCMD
	/* Exchange status with TCPCs */
	host_command_pd_send_status(PD_CHARGE_NO_CHANGE);
#endif
}

void board_tcpc_init(void)
{
	int i;

	/* Only reset TCPC if not sysjump */
	if (!system_jumped_to_this_image())
		board_reset_pd_mcu();

	/* Enable TCPCx interrupt */
	for (i = 0; i < CONFIG_USB_PD_PORT_MAX_COUNT; i++)
		gpio_enable_interrupt(tcpc_gpios[i].vbus.pin);
}
DECLARE_HOOK(HOOK_INIT, board_tcpc_init, HOOK_PRIO_INIT_I2C + 1);

static inline int board_dc_jack_present(void)
{
	return gpio_get_level(GPIO_DC_JACK_PRESENT);
}

static void board_dc_jack_handle(void)
{
	struct charge_port_info charge_dc_jack;

	/* System is booted from DC Jack */
	if (board_dc_jack_present()) {
		charge_dc_jack.current = (PD_MAX_POWER_MW * 1000) /
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

static void board_charge_init(void)
{
	int port, supplier;
	struct charge_port_info charge_init = {
		.current = 0,
		.voltage = USB_CHARGER_VOLTAGE_MV,
	};

	/* Initialize all charge suppliers to seed the charge manager */
	for (port = 0; port < CHARGE_PORT_COUNT; port++) {
		for (supplier = 0; supplier < CHARGE_SUPPLIER_COUNT; supplier++)
			charge_manager_update_charge(supplier, port,
				&charge_init);
	}

	board_dc_jack_handle();
}
DECLARE_HOOK(HOOK_INIT, board_charge_init, HOOK_PRIO_DEFAULT);

int board_set_active_charge_port(int port)
{
	int i;
	/* charge port is a realy physical port */
	int is_real_port = (port >= 0 &&
			port < CHARGE_PORT_COUNT);
	/* check if we are source vbus on that port */
	int source = board_vbus_source_enabled(port);

	if (is_real_port && source) {
		CPRINTS("Skip enable p%d", port);
		return EC_ERROR_INVAL;
	}

	/*
	 * Do not enable Type-C port if the DC Jack is present.
	 * When the Type-C is active port, hardware circuit will
	 * block DC jack from enabling +VADP_OUT.
	 */
	if (port != DEDICATED_CHARGE_PORT && board_dc_jack_present()) {
		CPRINTS("DC Jack present, Skip enable p%d", port);
		return EC_ERROR_INVAL;
	}

	/* Make sure non-charging ports are disabled */
	for (i = 0; i < CONFIG_USB_PD_PORT_MAX_COUNT; i++) {
		if (i == port)
			continue;

		board_charging_enable(i, 0);
	}

	/* Enable charging port */
	if (is_typec_port(port))
		board_charging_enable(port, 1);

	CPRINTS("New chg p%d", port);

	return EC_SUCCESS;
}

void board_set_charge_limit(int port, int supplier, int charge_ma,
			    int max_ma, int charge_mv)
{
	charge_set_input_current_limit(MAX(charge_ma,
				CONFIG_CHARGER_INPUT_CURRENT), charge_mv);
}
