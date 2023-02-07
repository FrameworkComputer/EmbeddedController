/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "charge_manager.h"
#include "charger.h"
#include "chipset.h"
#include "common.h"
#include "console.h"
#include "gpio.h"
#include "usb_pd.h"
#include "usbc_ppc.h"

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
	int prev_en;

	if (port < 0 || port >= board_get_usb_pd_port_count())
		return;

	prev_en = ppc_is_sourcing_vbus(port);

	/* Disable VBUS source */
	ppc_vbus_source_enable(port, 0);

	/* Enable discharge if we were previously sourcing 5V */
	if (prev_en)
		pd_set_vbus_discharge(port, 1);

	/* Notify host of power info change. */
	pd_send_host_event(PD_EVENT_POWER_CHANGE);
}

int pd_set_power_supply_ready(int port)
{
	int rv;

	/* Disable charging */
	rv = ppc_vbus_sink_enable(port, 0);
	if (rv)
		return rv;

	pd_set_vbus_discharge(port, 0);

	/* Enable VBUS source */
	rv = ppc_vbus_source_enable(port, 1);
	if (rv)
		return rv;

	/* Notify host of power info change. */
	pd_send_host_event(PD_EVENT_POWER_CHANGE);

	return EC_SUCCESS;
}

__override int pd_snk_is_vbus_provided(int port)
{
	if (port != CHARGE_PORT_TYPEC0)
		return 0;

	return ppc_is_vbus_present(port);
}
