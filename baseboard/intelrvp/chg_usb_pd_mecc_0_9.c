/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Intel-RVP family-specific configuration */

#include "console.h"
#include "hooks.h"
#include "tcpm/tcpci.h"
#include "system.h"

#define CPRINTF(format, args...) cprintf(CC_USBPD, format, ## args)
#define CPRINTS(format, args...) cprints(CC_USBPD, format, ## args)

/* Reset PD MCU */
void board_reset_pd_mcu(void)
{
	/* Add code if TCPC chips need a reset */
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
		ilim_en = (rp ==  TYPEC_RP_3A0 &&
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

#if CONFIG_DEDICATED_CHARGE_PORT_COUNT > 0
	if (port == DEDICATED_CHARGE_PORT)
		return 1;
#endif

	vbus_intr = gpio_get_level(tcpc_gpios[port].vbus.pin);

	return tcpc_gpios[port].vbus.pin_pol ? vbus_intr : !vbus_intr;
}

void tcpc_alert_event(enum gpio_signal signal)
{
	int i;

	for (i = 0; i < CONFIG_USB_PD_PORT_MAX_COUNT; i++) {
		if (tcpc_gpios[i].vbus.pin == signal) {
			schedule_deferred_pd_interrupt(i);
			break;
		}
	}
}

uint16_t tcpc_get_alert_status(void)
{
	uint16_t status = 0;
	int i;

	/* Check which port has the ALERT line set */
	for (i = 0; i < CHARGE_PORT_COUNT; i++) {
		/* No alerts for embdeded TCPC */
		if (tcpc_config[i].bus_type == EC_BUS_TYPE_EMBEDDED)
			continue;

		/* Add TCPC alerts if present */
	}

	return status;
}

void board_tcpc_init(void)
{
	int i;

	/* Only reset TCPC if not sysjump */
	if (!system_jumped_late())
		board_reset_pd_mcu();

	/* Enable TCPCx interrupt */
	for (i = 0; i < CONFIG_USB_PD_PORT_MAX_COUNT; i++)
		gpio_enable_interrupt(tcpc_gpios[i].vbus.pin);
}
DECLARE_HOOK(HOOK_INIT, board_tcpc_init, HOOK_PRIO_INIT_I2C + 1);
