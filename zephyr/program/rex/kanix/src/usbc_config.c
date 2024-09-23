/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cros_cbi.h"
#include "driver/ppc/ktu1125_public.h"
#include "driver/ppc/nx20p348x.h"
#include "driver/tcpm/ps8xxx_public.h"
#include "hooks.h"
#include "ppc/syv682x_public.h"
#include "system.h"
#include "usb_mux_config.h"
#include "usbc_config.h"

#include <stdbool.h>

#include <zephyr/drivers/espi.h>

#define CPRINTSUSB(format, args...) cprints(CC_USBCHARGE, format, ##args)
#define CPRINTFUSB(format, args...) cprintf(CC_USBCHARGE, format, ##args)

/* eSPI device */
#define espi_dev DEVICE_DT_GET(DT_CHOSEN(cros_ec_espi))

void board_reset_pd_mcu(void)
{
	/* Nothing to do */
}

__override bool board_is_tbt_usb4_port(int port)
{
	return true;
}

static void usbc_interrupt_init(void)
{
	/* Only reset TCPC if not sysjump */
	if (!system_jumped_late()) {
		board_reset_pd_mcu();
	}
}
DECLARE_HOOK(HOOK_INIT, usbc_interrupt_init, HOOK_PRIO_POST_I2C);

__override void board_overcurrent_event(int port, int is_overcurrented)
{
	/*
	 * Meteorlake PCH uses Virtual Wire for over current error,
	 * hence Send Over Current Virtual Wire' eSPI signal.
	 */
	espi_send_vwire(espi_dev, port + ESPI_VWIRE_SIGNAL_TARGET_GPIO_0,
			!is_overcurrented);
}

static void board_disable_charger_ports(void)
{
	int i;

	CPRINTSUSB("Disabling all charger ports");

	/* Disable all ports. */
	for (i = 0; i < board_get_usb_pd_port_count(); i++) {
		/*
		 * Do not return early if one fails otherwise we can
		 * get into a boot loop assertion failure.
		 */
		if (ppc_vbus_sink_enable(i, 0)) {
			CPRINTSUSB("Disabling C%d as sink failed.", i);
		}
	}
}

int board_set_active_charge_port(int port)
{
	bool is_valid_port = (port >= 0 && port < CONFIG_USB_PD_PORT_MAX_COUNT);
	int i;

	if (port == CHARGE_PORT_NONE) {
		board_disable_charger_ports();
		return EC_SUCCESS;
	} else if (!is_valid_port) {
		return EC_ERROR_INVAL;
	}

	/* Check if the port is sourcing VBUS. */
	if (ppc_is_sourcing_vbus(port)) {
		CPRINTSUSB("Skip enable C%d", port);
		return EC_ERROR_INVAL;
	}

	CPRINTSUSB("New charge port: C%d", port);

	/*
	 * Turn off the other ports' sink path FETs, before enabling the
	 * requested charge port.
	 */
	for (i = 0; i < board_get_usb_pd_port_count(); i++) {
		if (i == port) {
			continue;
		}
		if (ppc_vbus_sink_enable(i, 0)) {
			CPRINTSUSB("C%d: sink path disable failed.", i);
		}
	}

	/* Enable requested charge port. */
	if (ppc_vbus_sink_enable(port, 1)) {
		CPRINTSUSB("C%d: sink path enable failed.", port);
		return EC_ERROR_UNKNOWN;
	}

	return EC_SUCCESS;
}
