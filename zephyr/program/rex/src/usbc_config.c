/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "battery_fuel_gauge.h"
#include "charge_manager.h"
#include "charge_ramp.h"
#include "charge_state.h"
#include "charger.h"
#include "driver/charger/isl9241.h"
#include "driver/ppc/nx20p348x.h"
#include "driver/retimer/bb_retimer_public.h"
#include "driver/tcpm/nct38xx.h"
#include "driver/tcpm/tcpci.h"
#include "gpio/gpio_int.h"
#include "hooks.h"
#include "i2c.h"
#include "ioexpander.h"
#include "ppc/syv682x_public.h"
#include "system.h"
#include "task.h"
#include "usb_mux.h"
#include "usbc_config.h"
#include "usbc_ppc.h"

#include <stdbool.h>

#include <zephyr/drivers/espi.h>
#include <zephyr/drivers/gpio.h>

#define CPRINTSUSB(format, args...) cprints(CC_USBCHARGE, format, ##args)
#define CPRINTFUSB(format, args...) cprintf(CC_USBCHARGE, format, ##args)

/* eSPI device */
#define espi_dev DEVICE_DT_GET(DT_CHOSEN(cros_ec_espi))

/*******************************************************************/
/* USB-C Configuration Start */

static void usbc_interrupt_init(void)
{
	/* Only reset TCPC if not sysjump */
	if (!system_jumped_late()) {
		board_reset_pd_mcu();
	}

	/* Enable BC 1.2 interrupts */
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_usb_c0_bc12));
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_usb_c1_bc12));

	/* Enable SBU fault interrupts */
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_usb_c0_sbu_fault));
}
DECLARE_HOOK(HOOK_INIT, usbc_interrupt_init, HOOK_PRIO_POST_I2C);

__override void board_overcurrent_event(int port, int is_overcurrented)
{
	/*
	 * Meteorlake PCH uses Virtual Wire for over current error,
	 * hence Send 'Over Current Virtual Wire' eSPI signal.
	 */
	espi_send_vwire(espi_dev, port + ESPI_VWIRE_SIGNAL_SLV_GPIO_0,
			!is_overcurrented);
}

void sbu_fault_interrupt(enum gpio_signal signal)
{
	int port = USBC_PORT_C0;

	CPRINTSUSB("C%d: SBU fault", port);
	pd_handle_overcurrent(port);
}

void reset_nct38xx_port(int port)
{
	const struct gpio_dt_spec *reset_gpio_l;
	const struct device *ioex_port0, *ioex_port1;

	/* TODO(b/225189538): Save and restore ioex signals */
	if (port == USBC_PORT_C0) {
		reset_gpio_l = &tcpc_config[0].rst_gpio;
		ioex_port0 = DEVICE_DT_GET(DT_NODELABEL(ioex_c0_port0));
		ioex_port1 = DEVICE_DT_GET(DT_NODELABEL(ioex_c0_port1));
#if DT_NODE_EXISTS(DT_NODELABEL(nct3807_c1))
	} else if (port == USBC_PORT_C1) {
		reset_gpio_l = &tcpc_config[1].rst_gpio;
		ioex_port0 = DEVICE_DT_GET(DT_NODELABEL(ioex_c1_port0));
		ioex_port1 = DEVICE_DT_GET(DT_NODELABEL(ioex_c1_port1));
#endif
	} else {
		/* Invalid port: do nothing */
		return;
	}

	gpio_pin_set_dt(reset_gpio_l, 1);
	msleep(NCT38XX_RESET_HOLD_DELAY_MS);
	gpio_pin_set_dt(reset_gpio_l, 0);
	nct38xx_reset_notify(port);
	if (NCT3807_RESET_POST_DELAY_MS != 0) {
		msleep(NCT3807_RESET_POST_DELAY_MS);
	}

	/* Re-enable the IO expander pins */
	gpio_reset_port(ioex_port0);
	gpio_reset_port(ioex_port1);
}

void bc12_interrupt(enum gpio_signal signal)
{
	switch (signal) {
	case GPIO_USB_C0_BC12_INT_ODL:
		usb_charger_task_set_event(0, USB_CHG_EVENT_BC12);
		break;
	case GPIO_USB_C1_BC12_INT_ODL:
		usb_charger_task_set_event(1, USB_CHG_EVENT_BC12);
		break;
	default:
		break;
	}
}

static void board_disable_charger_ports(void)
{
	int i;

	CPRINTSUSB("Disabling all charger ports");

	/* Disable all ports. */
	for (i = 0; i < board_get_usb_pd_port_count(); i++) {
		/*
		 * If this port had booted in dead battery mode, go
		 * ahead and reset it so EN_SNK responds properly.
		 */
		if (nct38xx_get_boot_type(i) == NCT38XX_BOOT_DEAD_BATTERY) {
			reset_nct38xx_port(i);
			pd_set_error_recovery(i);
		}

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

	/*
	 * Check if we can reset any ports in dead battery mode
	 *
	 * The NCT3807 may continue to keep EN_SNK low on the dead battery port
	 * and allow a dangerous level of voltage to pass through to the initial
	 * charge port (see b/183660105).  We must reset the ports if we have
	 * sufficient battery to do so, which will bring EN_SNK back under
	 * normal control.
	 */
	if (port == USBC_PORT_C0 &&
	    nct38xx_get_boot_type(port) == NCT38XX_BOOT_DEAD_BATTERY) {
		/* Handle dead battery boot case */
		CPRINTSUSB("Found dead battery on C0");
		/*
		 * If we have battery, get this port reset ASAP.
		 * This means temporarily rejecting charge manager
		 * sets to it.
		 */
		if (pd_is_battery_capable()) {
			reset_nct38xx_port(port);
			pd_set_error_recovery(port);
		}
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
