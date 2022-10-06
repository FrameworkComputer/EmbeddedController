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

	if (port < 0 || port >= CONFIG_USB_PD_PORT_MAX_COUNT)
		return;

	/* TODO(b/147440290): charger functions should take chgnum */
	prev_en = chg_chips[port].drv->is_sourcing_otg_power(port, port);

	/* Disable Vbus */
	chg_chips[port].drv->enable_otg_power(port, 0);

	/* Discharge Vbus if previously enabled */
	if (prev_en)
		sm5803_set_vbus_disch(port, 1);

	/* Notify host of power info change. */
	pd_send_host_event(PD_EVENT_POWER_CHANGE);
}

int pd_set_power_supply_ready(int port)
{
	enum ec_error_list rv;

	/* Disable charging */
	rv = chg_chips[port].drv->set_mode(port, CHARGE_FLAG_INHIBIT_CHARGE);
	if (rv)
		return rv;

	/* Disable Vbus discharge */
	sm5803_set_vbus_disch(port, 0);

	/* Provide Vbus */
	chg_chips[port].drv->enable_otg_power(port, 1);

	/* Notify host of power info change. */
	pd_send_host_event(PD_EVENT_POWER_CHANGE);

	return EC_SUCCESS;
}

int pd_snk_is_vbus_provided(int port)
{
	int chg_det = 0;

	sm5803_get_chg_det(port, &chg_det);

	return chg_det;
}
