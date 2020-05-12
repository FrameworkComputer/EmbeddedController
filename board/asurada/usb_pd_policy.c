/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "charge_manager.h"
#include "usb_pd.h"
#include "usbc_ppc.h"

#ifndef CONFIG_USB_PD_MAX_SINGLE_SOURCE_CURRENT
#error Asurada reference must define CONFIG_USB_PD_MAX_SINGLE_SOURCE_CURRENT
#endif

int svdm_get_hpd_gpio(int port)
{
	/* HPD is low active, inverse the result */
	return !gpio_get_level(GPIO_EC_DPBRDG_HPD_ODL);
}

void svdm_set_hpd_gpio(int port, int en)
{
	/*
	 * HPD is low active, inverse the en
	 * TODO: C0&C1 shares the same HPD, implement FCFS policy.
	 */
	gpio_set_level(GPIO_EC_DPBRDG_HPD_ODL, !en);
}

int pd_snk_is_vbus_provided(int port)
{
	return ppc_is_vbus_present(port);
}

void pd_power_supply_reset(int port)
{
	int prev_en;

	prev_en = ppc_is_sourcing_vbus(port);

	/* Disable VBUS. */
	ppc_vbus_source_enable(port, 0);

	/* Enable discharge if we were previously sourcing 5V */
	if (prev_en)
		pd_set_vbus_discharge(port, 1);

#ifdef CONFIG_USB_PD_MAX_SINGLE_SOURCE_CURRENT
	/* Give back the current quota we are no longer using */
	charge_manager_source_port(port, 0);
#endif /* defined(CONFIG_USB_PD_MAX_SINGLE_SOURCE_CURRENT) */

	/* Notify host of power info change. */
	pd_send_host_event(PD_EVENT_POWER_CHANGE);
}

int pd_check_vconn_swap(int port)
{
	/* TODO: Only allow vconn swap if PP4200_G rail is enabled , s3/s0 on */
	return 0;
}

int pd_set_power_supply_ready(int port)
{
	int rv;

	/* Disable charging. */
	rv = ppc_vbus_sink_enable(port, 0);
	if (rv)
		return rv;

	pd_set_vbus_discharge(port, 0);

	/* Provide Vbus. */
	rv = ppc_vbus_source_enable(port, 1);
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

int board_vbus_source_enabled(int port)
{
	return ppc_is_sourcing_vbus(port);
}

