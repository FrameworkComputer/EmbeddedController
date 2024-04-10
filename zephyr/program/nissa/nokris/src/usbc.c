/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "charge_state.h"
#include "chipset.h"
#include "driver/tcpm/nct38xx.h"
#include "driver/tcpm/ps8xxx_public.h"
#include "driver/tcpm/tcpci.h"
#include "gpio.h"
#include "hooks.h"
#include "nissa_sub_board.h"
#include "system.h"
#include "usb_mux.h"
#include "usbc_ppc.h"

#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(nissa, CONFIG_NISSA_LOG_LEVEL);

#define CPRINTSUSB(format, args...) cprints(CC_USBCHARGE, format, ##args)
#define CPRINTFUSB(format, args...) cprintf(CC_USBCHARGE, format, ##args)

enum usbc_port { USBC_PORT_C0 = 0, USBC_PORT_C1, USBC_PORT_COUNT };

/* Used by USB charger task with CONFIG_USB_PD_5V_EN_CUSTOM */
int board_is_sourcing_vbus(int port)
{
	return board_vbus_source_enabled(port);
}

int board_set_active_charge_port(int port)
{
	int is_valid_port = board_is_usb_pd_port_present(port);
	int i;

	if (port == CHARGE_PORT_NONE) {
		CPRINTSUSB("Disabling all charger ports");

		/* Disable all ports. */
		for (i = 0; i < ppc_cnt; i++) {
			/*
			 * Do not return early if one fails otherwise we can
			 * get into a boot loop assertion failure.
			 */
			if (ppc_vbus_sink_enable(i, 0))
				CPRINTSUSB("Disabling C%d as sink failed.", i);
		}

		return EC_SUCCESS;
	} else if (!is_valid_port) {
		return EC_ERROR_INVAL;
	}

	/* Check if the port is sourcing VBUS. */
	if (board_is_sourcing_vbus(port)) {
		CPRINTFUSB("Skip enable C%d", port);
		return EC_ERROR_INVAL;
	}

	CPRINTSUSB("New charge port: C%d", port);

	/*
	 * Turn off the other ports' sink path FETs, before enabling the
	 * requested charge port.
	 */
	for (i = 0; i < ppc_cnt; i++) {
		if (i == port)
			continue;

		if (ppc_vbus_sink_enable(i, 0))
			CPRINTSUSB("C%d: sink path disable failed.", i);
	}

	/* Enable requested charge port. */
	if (ppc_vbus_sink_enable(port, 1)) {
		CPRINTSUSB("C%d: sink path enable failed.", port);
		return EC_ERROR_UNKNOWN;
	}

	return EC_SUCCESS;
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
#if DT_NODE_EXISTS(DT_NODELABEL(nct3807_C1))
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
	crec_msleep(NCT38XX_RESET_HOLD_DELAY_MS);
	gpio_pin_set_dt(reset_gpio_l, 0);
	nct38xx_reset_notify(port);
	if (NCT3807_RESET_POST_DELAY_MS != 0) {
		crec_msleep(NCT3807_RESET_POST_DELAY_MS);
	}

	/* Re-enable the IO expander pins */
	gpio_reset_port(ioex_port0);
	gpio_reset_port(ioex_port1);
}

void pd_power_supply_reset(int port)
{
	/* Disable VBUS. */
	ppc_vbus_source_enable(port, 0);

	/* Enable discharge if we were previously sourcing 5V */
	if (IS_ENABLED(CONFIG_USB_PD_DISCHARGE))
		pd_set_vbus_discharge(port, 1);

	/* Notify host of power info change. */
	pd_send_host_event(PD_EVENT_POWER_CHANGE);
}

int pd_set_power_supply_ready(int port)
{
	int rv;

	/* Disable charging. */
	rv = ppc_vbus_sink_enable(port, 0);
	if (rv)
		return rv;

	if (IS_ENABLED(CONFIG_USB_PD_DISCHARGE)) {
		pd_set_vbus_discharge(port, 0);
	}

	/* Provide Vbus. */
	rv = ppc_vbus_source_enable(port, 1);
	if (rv) {
		return rv;
	}

	/* Notify host of power info change. */
	pd_send_host_event(PD_EVENT_POWER_CHANGE);

	return EC_SUCCESS;
}

void board_reset_pd_mcu(void)
{
	/* Reset TCPC0 */
	reset_nct38xx_port(USBC_PORT_C0);

	/* Reset TCPC1 */
	if (nissa_get_sb_type() == NISSA_SB_C_A &&
	    tcpc_config[USBC_PORT_C1].rst_gpio.port) {
		gpio_pin_set_dt(&tcpc_config[USBC_PORT_C1].rst_gpio, 1);
		crec_msleep(PS8XXX_RESET_DELAY_MS);
		gpio_pin_set_dt(&tcpc_config[USBC_PORT_C1].rst_gpio, 0);
		crec_msleep(PS8815_FW_INIT_DELAY_MS);
	}
}

void bc12_interrupt(enum gpio_signal signal)
{
	if (signal == GPIO_USB_C0_BC12_INT_ODL)
		usb_charger_task_set_event(0, USB_CHG_EVENT_BC12);
	else
		usb_charger_task_set_event(1, USB_CHG_EVENT_BC12);
}

/* Used by Vbus discharge common code with CONFIG_USB_PD_DISCHARGE */
int board_vbus_source_enabled(int port)
{
	return ppc_is_sourcing_vbus(port);
}
