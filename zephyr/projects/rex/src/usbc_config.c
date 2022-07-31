/* Copyright 2022 The ChromiumOS Authors.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/drivers/gpio.h>

#include "battery_fuel_gauge.h"
#include "charger.h"
#include "charge_manager.h"
#include "charge_ramp.h"
#include "charge_state_v2.h"
#include "charge_state.h"
#include "charger.h"
#include "driver/charger/isl9241.h"
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
#include "usbc_ppc.h"

#define CPRINTSUSB(format, args...) cprints(CC_USBCHARGE, format, ##args)
#define CPRINTFUSB(format, args...) cprintf(CC_USBCHARGE, format, ##args)

/*******************************************************************/
/* USB-C Configuration Start */
#define GPIO_USB_C0_TCPC_INT_NODE \
	GPIO_DT_FROM_NODELABEL(gpio_usb_c0_tcpc_int_odl)
#define GPIO_USB_C0_TCPC_RST_NODE \
	GPIO_DT_FROM_NODELABEL(gpio_usb_c0_tcpc_rst_odl)

/* USB-C ports */
enum usbc_port { USBC_PORT_C0 = 0, USBC_PORT_COUNT };
BUILD_ASSERT(USBC_PORT_COUNT == CONFIG_USB_PD_PORT_MAX_COUNT);

static void usbc_interrupt_init(void)
{
	/* Enable PPC interrupts. */
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_usb_c0_ppc));

	/* Enable TCPC interrupts. */
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_usb_c0_tcpc));

	/* Enable BC 1.2 interrupts */
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_usb_c0_bc12));

	/* Enable SBU fault interrupts */
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_usb_c0_sbu_fault));
}
DECLARE_HOOK(HOOK_INIT, usbc_interrupt_init, HOOK_PRIO_POST_I2C);

void board_overcurrent_event(int port, int is_overcurrented)
{
	/*
	 * TODO: Meteorlake PCH does not use Physical GPIO for over current
	 * error, hence Send 'Over Current Virtual Wire' eSPI signal.
	 */
}

void sbu_fault_interrupt(enum gpio_signal signal)
{
	int port = USBC_PORT_C0;

	CPRINTSUSB("C%d: SBU fault", port);
	pd_handle_overcurrent(port);
}

void tcpc_alert_event(enum gpio_signal signal)
{
	int port;

	switch (signal) {
	case GPIO_USB_C0_TCPC_INT_ODL:
		port = 0;
		break;
	default:
		return;
	}

	schedule_deferred_pd_interrupt(port);
}

static void reset_nct38xx_port(int port)
{
	const struct gpio_dt_spec *reset_gpio_l;
	const struct device *ioex_port0, *ioex_port1;

	/* TODO(b/225189538): Save and restore ioex signals */
	if (port == USBC_PORT_C0) {
		reset_gpio_l = GPIO_DT_FROM_NODELABEL(gpio_usb_c0_tcpc_rst_odl);
		ioex_port0 = DEVICE_DT_GET(DT_NODELABEL(ioex_c0_port0));
		ioex_port1 = DEVICE_DT_GET(DT_NODELABEL(ioex_c0_port1));
	} else {
		/* Invalid port: do nothing */
		return;
	}

	gpio_pin_set_dt(reset_gpio_l, 0);
	msleep(NCT38XX_RESET_HOLD_DELAY_MS);
	gpio_pin_set_dt(reset_gpio_l, 1);
	nct38xx_reset_notify(port);
	if (NCT3807_RESET_POST_DELAY_MS != 0) {
		msleep(NCT3807_RESET_POST_DELAY_MS);
	}

	/* Re-enable the IO expander pins */
	gpio_reset_port(ioex_port0);
	gpio_reset_port(ioex_port1);
}

void board_reset_pd_mcu(void)
{
	/* Reset TCPC0 */
	reset_nct38xx_port(USBC_PORT_C0);
}

uint16_t tcpc_get_alert_status(void)
{
	uint16_t status = 0;

	/*
	 * Check which port has the ALERT line set and ignore if that TCPC has
	 * its reset line active.
	 */
	if (!GPIO_USB_C0_TCPC_INT_NODE && GPIO_USB_C0_TCPC_RST_NODE) {
		status |= PD_STATUS_TCPC_ALERT_0;
	}

	return status;
}

void ppc_interrupt(enum gpio_signal signal)
{
	switch (signal) {
	case GPIO_USB_C0_PPC_INT_ODL:
		syv682x_interrupt(USBC_PORT_C0);
		break;

	default:
		break;
	}
}

void bc12_interrupt(enum gpio_signal signal)
{
	switch (signal) {
	case GPIO_USB_C0_BC12_INT_ODL:
		usb_charger_task_set_event(0, USB_CHG_EVENT_BC12);
		break;

	default:
		break;
	}
}

void board_set_charge_limit(int port, int supplier, int charge_ma, int max_ma,
			    int charge_mv)
{
	charge_set_input_current_limit(
		MAX(charge_ma, CONFIG_CHARGER_INPUT_CURRENT), charge_mv);
}

static void board_disable_charger_ports(void)
{
	int i;

	CPRINTSUSB("Disabling all charger ports");

	/* Disable all ports. */
	for (i = 0; i < ppc_cnt; i++) {
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
	int is_valid_port = (port >= 0 && port < CONFIG_USB_PD_PORT_MAX_COUNT);
	int i;
	int rv;

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
	rv = EC_SUCCESS;
	for (i = 0; i < board_get_usb_pd_port_count(); i++) {
		if (nct38xx_get_boot_type(i) != NCT38XX_BOOT_DEAD_BATTERY) {
			continue;
		}

		/* Handle dead battery boot case */
		CPRINTSUSB("Found dead battery on %d", i);
		/*
		 * If we have battery, get this port reset ASAP.
		 * This means temporarily rejecting charge manager
		 * sets to it.
		 */
		if (pd_is_battery_capable()) {
			reset_nct38xx_port(i);
			pd_set_error_recovery(i);

			if (port == i) {
				rv = EC_ERROR_INVAL;
			}
		} else if (port != i) {
			/*
			 * If other port is selected and in dead battery
			 * mode, reset this port.  Otherwise, reject
			 * change because we'll brown out.
			 */
			if (nct38xx_get_boot_type(port) ==
			    NCT38XX_BOOT_DEAD_BATTERY) {
				reset_nct38xx_port(i);
				pd_set_error_recovery(i);
			} else {
				rv = EC_ERROR_INVAL;
			}
		}
	}

	if (rv != EC_SUCCESS) {
		return rv;
	}

	/* Check if the port is sourcing VBUS. */
	if (tcpm_get_src_ctrl(port)) {
		CPRINTSUSB("Skip enable C%d", port);
		return EC_ERROR_INVAL;
	}

	CPRINTSUSB("New charge port: C%d", port);

	/*
	 * Turn off the other ports' sink path FETs, before enabling the
	 * requested charge port.
	 */
	for (i = 0; i < ppc_cnt; i++) {
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
