/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Shared USB-C policy for Zork boards */

#include "charge_manager.h"
#include "chipset.h"
#include "common.h"
#include "compile_time_macros.h"
#include "console.h"
#include "ec_commands.h"
#include "gpio.h"
#include "system.h"
#include "usb_mux.h"
#include "usb_pd.h"
#include "usbc_ppc.h"
#include "util.h"

int pd_check_vconn_swap(int port)
{
	/*
	 * Do not allow vconn swap 5V rail is off
	 * S5_PGOOD depends on PG_PP5000_S5 being asserted,
	 * so GPIO_S5_PGOOD is a reasonable proxy for PP5000_S5
	 */
	return gpio_get_level(GPIO_S5_PGOOD);
}

void pd_power_supply_reset(int port)
{
	/*
	 * Don't need to shutoff VBus if we are not sourcing it
	 * TODO: Ensure Vbus sourcing is being disabled appropriately to
	 *       avoid invalid TC states
	 */
	if (ppc_is_sourcing_vbus(port)) {
		/* Disable VBUS. */
		ppc_vbus_source_enable(port, 0);

		/* Enable discharge if we were previously sourcing 5V */
		if (IS_ENABLED(CONFIG_USB_PD_DISCHARGE))
			pd_set_vbus_discharge(port, 1);
	}

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

	if (IS_ENABLED(CONFIG_USB_PD_DISCHARGE))
		pd_set_vbus_discharge(port, 0);

	/* Provide Vbus. */
	rv = ppc_vbus_source_enable(port, 1);
	if (rv)
		return rv;

	/* Notify host of power info change. */
	pd_send_host_event(PD_EVENT_POWER_CHANGE);

	return EC_SUCCESS;
}

int board_vbus_source_enabled(int port)
{
	/* Answer is always "no" from the BJ port */
	if (port >= CONFIG_USB_PD_PORT_MAX_COUNT)
		return 0;

	return ppc_is_sourcing_vbus(port);
}
