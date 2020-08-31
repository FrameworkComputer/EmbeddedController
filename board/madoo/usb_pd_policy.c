/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "charge_manager.h"
#include "chipset.h"
#include "common.h"
#include "console.h"
#include "driver/tcpm/tcpci.h"
#include "usb_pd.h"

#define CPRINTF(format, args...) cprintf(CC_USBPD, format, ## args)
#define CPRINTS(format, args...) cprints(CC_USBPD, format, ## args)

int pd_check_vconn_swap(int port)
{
	/* Allow VCONN swaps if the AP is on. */
	return chipset_in_state(CHIPSET_STATE_ANY_SUSPEND | CHIPSET_STATE_ON);
}

void pd_power_supply_reset(int port)
{
	/* Disable VBUS */
	tcpc_write(port, TCPC_REG_COMMAND, TCPC_REG_COMMAND_SRC_CTRL_LOW);

#ifdef CONFIG_USB_PD_MAX_SINGLE_SOURCE_CURRENT
	/* Give back the current quota we are no longer using */
	charge_manager_source_port(port, 0);
#endif /* defined(CONFIG_USB_PD_MAX_SINGLE_SOURCE_CURRENT) */

	/* Notify host of power info change. */
	pd_send_host_event(PD_EVENT_POWER_CHANGE);
}

int pd_set_power_supply_ready(int port)
{
	int rv;

	if (port >= CONFIG_USB_PD_PORT_MAX_COUNT)
		return EC_ERROR_INVAL;

	/* Disable charging. */
	rv = tcpc_write(port, TCPC_REG_COMMAND, TCPC_REG_COMMAND_SNK_CTRL_LOW);
	if (rv)
		return rv;

	/* Our policy is not to source VBUS when the AP is off. */
	if (chipset_in_state(CHIPSET_STATE_ANY_OFF))
		return EC_ERROR_NOT_POWERED;

	/* Provide Vbus. */
	rv = tcpc_write(port, TCPC_REG_COMMAND, TCPC_REG_COMMAND_SRC_CTRL_HIGH);
	if (rv)
		return rv;

#ifdef CONFIG_USB_PD_MAX_SINGLE_SOURCE_CURRENT
	/* Ensure we advertise the proper available current quota */
	charge_manager_source_port(port, 1);
#endif /* defined(CONFIG_USB_PD_MAX_SINGLE_SOURCE_CURRENT) */

	/* Notify host of power info change. */
	pd_send_host_event(PD_EVENT_POWER_CHANGE);

	return EC_SUCCESS;
}
