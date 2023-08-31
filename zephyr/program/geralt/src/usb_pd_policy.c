/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "adc.h"
#include "charge_manager.h"
#include "chipset.h"
#include "usb_charge.h"
#include "usb_pd.h"
#include "usb_pd_policy.h"
#include "usbc_ppc.h"

int pd_check_vconn_swap(int port)
{
	/* Allow Vconn swap if AP is on. */
	return chipset_in_state(CHIPSET_STATE_SUSPEND | CHIPSET_STATE_ON);
}

int pd_snk_is_vbus_provided(int port)
{
	__maybe_unused static atomic_t vbus_prev[CONFIG_USB_PD_PORT_MAX_COUNT];
	int vbus;

	/*
	 * (b:181203590#comment20) TODO(yllin): use
	 *  PD_VSINK_DISCONNECT_PD for non-5V case.
	 */
	vbus = adc_read_channel(board_get_vbus_adc(port)) >=
	       PD_V_SINK_DISCONNECT_MAX;

#ifdef CONFIG_USB_CHARGER
	/*
	 * There's no PPC to inform VBUS change for usb_charger, so inform
	 * the usb_charger now.
	 */
	if (!!(vbus_prev[port] != vbus)) {
		usb_charger_vbus_change(port, vbus);
	}

	if (vbus) {
		atomic_or(&vbus_prev[port], 1);
	} else {
		atomic_clear(&vbus_prev[port]);
	}
#endif
	return vbus;
}

void pd_power_supply_reset(int port)
{
	int prev_en;

	prev_en = ppc_is_sourcing_vbus(port);

	/* Disable VBUS. */
	ppc_vbus_source_enable(port, 0);

	/* Enable discharge if we were previously sourcing 5V */
	if (prev_en) {
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
	if (rv) {
		return rv;
	}

	pd_set_vbus_discharge(port, 0);

	/* Provide Vbus. */
	rv = ppc_vbus_source_enable(port, 1);
	if (rv) {
		return rv;
	}

	/* Notify host of power info change. */
	pd_send_host_event(PD_EVENT_POWER_CHANGE);

	return EC_SUCCESS;
}

__override bool port_frs_disable_until_source_on(int port)
{
	return true;
}

int board_vbus_source_enabled(int port)
{
	return ppc_is_sourcing_vbus(port);
}
