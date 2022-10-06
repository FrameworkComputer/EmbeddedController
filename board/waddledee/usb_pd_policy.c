/* Copyright 2020 The ChromiumOS Authors
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

#define CPRINTF(format, args...) cprintf(CC_USBPD, format, ##args)
#define CPRINTS(format, args...) cprints(CC_USBPD, format, ##args)

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

	/* Notify host of power info change. */
	pd_send_host_event(PD_EVENT_POWER_CHANGE);

	return EC_SUCCESS;
}

__override bool pd_check_vbus_level(int port, enum vbus_level level)
{
	return sm5803_check_vbus_level(port, level);
}

int pd_snk_is_vbus_provided(int port)
{
	return sm5803_is_vbus_present(port);
}
