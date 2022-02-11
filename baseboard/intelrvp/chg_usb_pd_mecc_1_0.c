/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Intel-RVP family-specific configuration */

#include "charge_manager.h"
#include "charge_state_v2.h"
#include "console.h"
#include "driver/ppc/sn5s330.h"
#include "gpio.h"
#include "hooks.h"
#include "tcpm/tcpci.h"
#include "system.h"
#include "usbc_ppc.h"

#define CPRINTF(format, args...) cprintf(CC_USBPD, format, ## args)
#define CPRINTS(format, args...) cprints(CC_USBPD, format, ## args)

/* Reset PD MCU */
void board_reset_pd_mcu(void)
{
	/* Add code if TCPC chips need a reset */
}

static void baseboard_tcpc_init(void)
{
	int i;

	/* Only reset TCPC if not sysjump */
	if (!system_jumped_late())
		board_reset_pd_mcu();

	for (i = 0; i < CONFIG_USB_PD_PORT_MAX_COUNT; i++) {
		/* Enable PPC interrupts. */
		if (tcpc_aic_gpios[i].ppc_intr_handler)
			gpio_enable_interrupt(tcpc_aic_gpios[i].ppc_alert);

		/* Enable TCPC interrupts. */
		if (tcpc_config[i].bus_type != EC_BUS_TYPE_EMBEDDED)
			gpio_enable_interrupt(tcpc_aic_gpios[i].tcpc_alert);
	}
}
DECLARE_HOOK(HOOK_INIT, baseboard_tcpc_init, HOOK_PRIO_INIT_CHIPSET);

void tcpc_alert_event(enum gpio_signal signal)
{
	int i;

	for (i = 0; i < CONFIG_USB_PD_PORT_MAX_COUNT; i++) {
		/* No alerts for embdeded TCPC */
		if (tcpc_config[i].bus_type == EC_BUS_TYPE_EMBEDDED)
			continue;

		if (signal == tcpc_aic_gpios[i].tcpc_alert) {
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
	for (i = 0; i < CONFIG_USB_PD_PORT_MAX_COUNT; i++) {
		/* No alerts for embdeded TCPC */
		if (tcpc_config[i].bus_type == EC_BUS_TYPE_EMBEDDED)
			continue;

		if (!gpio_get_level(tcpc_aic_gpios[i].tcpc_alert))
			status |= PD_STATUS_TCPC_ALERT_0 << i;
	}

	return status;
}

int ppc_get_alert_status(int port)
{
	if (!tcpc_aic_gpios[port].ppc_intr_handler)
		return 0;

	return !gpio_get_level(tcpc_aic_gpios[port].ppc_alert);
}

/* PPC support routines */
void ppc_interrupt(enum gpio_signal signal)
{
	int i;

	for (i = 0; i < CONFIG_USB_PD_PORT_MAX_COUNT; i++) {
		if (tcpc_aic_gpios[i].ppc_intr_handler &&
			signal == tcpc_aic_gpios[i].ppc_alert) {
			tcpc_aic_gpios[i].ppc_intr_handler(i);
			break;
		}
	}
}

void board_charging_enable(int port, int enable)
{
	if (ppc_vbus_sink_enable(port, enable))
		CPRINTS("C%d: sink path %s failed",
				port, enable ? "en" : "dis");
}
