/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "charge_manager.h"
#include "charger.h"
#include "chipset.h"
#include "common.h"
#include "console.h"
#include "driver/tcpm/tcpci.h"
#include "gpio.h"
#include "usb_pd.h"

#define CPRINTF(format, args...) cprintf(CC_USBPD, format, ##args)
#define CPRINTS(format, args...) cprints(CC_USBPD, format, ##args)

__override int pd_check_power_swap(int port)
{
	/* If type-c port is supplying power, we never swap PR (to source) */
	if (port == charge_manager_get_active_charge_port())
		return 0;
	/*
	 * Allow power swap as long as we are acting as a dual role device,
	 * otherwise assume our role is fixed (not in S0 or console command
	 * to fix our role).
	 */
	return (pd_get_dual_role(port) == PD_DRP_TOGGLE_ON ? 1 : 0);
}

int pd_check_vconn_swap(int port)
{
	/* Allow VCONN swaps if the AP is on */
	return gpio_get_level(GPIO_EN_PP5000_U);
}

void pd_power_supply_reset(int port)
{
	if (port < 0 || port >= board_get_usb_pd_port_count())
		return;

	/* Disable VBUS source */
	/* TODO(b/267742066): Actually disable VBUS */
	/* gpio_set_level(GPIO_EN_USB_C0_VBUS, 0); */

	/* Notify host of power info change. */
	pd_send_host_event(PD_EVENT_POWER_CHANGE);
}

int pd_set_power_supply_ready(int port)
{
	/* Disable charging */
	/* TODO(b/267742066): Actually disable charging */
	/* gpio_set_level(GPIO_EN_PPVAR_USBC_ADP_L, 1); */

	/* Enable VBUS source */
	/* TODO(b/267742066): Actually enable VBUS */
	/* gpio_set_level(GPIO_EN_USB_C0_VBUS, 1); */

	/* Notify host of power info change. */
	pd_send_host_event(PD_EVENT_POWER_CHANGE);

	return EC_SUCCESS;
}

__override int pd_snk_is_vbus_provided(int port)
{
	if (port != CHARGE_PORT_TYPEC0)
		return 0;

	return tcpm_check_vbus_level(port, VBUS_PRESENT);
}
