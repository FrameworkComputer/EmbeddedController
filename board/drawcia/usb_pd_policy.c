/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "battery_smart.h"
#include "charge_manager.h"
#include "charger.h"
#include "chipset.h"
#include "common.h"
#include "console.h"
#include "driver/charger/sm5803.h"
#include "driver/tcpm/tcpci.h"
#include "usb_pd.h"

#define CPRINTF(format, args...) cprintf(CC_USBPD, format, ## args)
#define CPRINTS(format, args...) cprints(CC_USBPD, format, ## args)

int pd_check_vconn_swap(int port)
{
	/* Allow VCONN swaps if the AP is on */
	return chipset_in_state(CHIPSET_STATE_ANY_SUSPEND | CHIPSET_STATE_ON);
}

void pd_power_supply_reset(int port)
{
	int prev_en;

	if (port < 0 || port >= board_get_usb_pd_port_count())
		return;

	prev_en = charger_is_sourcing_otg_power(port);

	/* Disable Vbus */
	charger_enable_otg_power(port, 0);

	/* Discharge Vbus if previously enabled */
	if (prev_en)
		sm5803_set_vbus_disch(port, 1);

#ifdef CONFIG_USB_PD_MAX_SINGLE_SOURCE_CURRENT
	/* Give back the current quota we are no longer using */
	charge_manager_source_port(port, 0);
#endif /* defined(CONFIG_USB_PD_MAX_SINGLE_SOURCE_CURRENT) */

	/* Notify host of power info change. */
	pd_send_host_event(PD_EVENT_POWER_CHANGE);
}

int pd_set_power_supply_ready(int port)
{
	enum ec_error_list rv;

	/* Disable sinking */
	rv = sm5803_vbus_sink_enable(port, 0);
	if (rv)
		return rv;

	/* Disable Vbus discharge */
	sm5803_set_vbus_disch(port, 0);

	/* Provide Vbus */
	charger_enable_otg_power(port, 1);

#ifdef CONFIG_USB_PD_MAX_SINGLE_SOURCE_CURRENT
	/* Ensure we advertise the proper available current quota */
	charge_manager_source_port(port, 1);
#endif /* defined(CONFIG_USB_PD_MAX_SINGLE_SOURCE_CURRENT) */

	/* Notify host of power info change. */
	pd_send_host_event(PD_EVENT_POWER_CHANGE);

	return EC_SUCCESS;
}

__override bool pd_check_vbus_level(int port, enum vbus_level level)
{
	int vbus_voltage;

	/* If we're unable to speak to the charger, best to guess false */
	if (charger_get_vbus_voltage(port, &vbus_voltage))
		return false;

	if (level == VBUS_SAFE0V)
		return vbus_voltage < PD_V_SAFE0V_MAX;
	else if (level == VBUS_PRESENT)
		return vbus_voltage > PD_V_SAFE5V_MIN;
	else
		return vbus_voltage < PD_V_SINK_DISCONNECT_MAX;
}

int pd_snk_is_vbus_provided(int port)
{
	return sm5803_is_vbus_present(port);
}
