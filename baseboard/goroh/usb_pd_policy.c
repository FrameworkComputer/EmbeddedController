/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "charge_manager.h"
#include "chipset.h"
#include "gpio.h"
#include "timer.h"
#include "usb_dp_alt_mode.h"
#include "usb_mux.h"
#include "usb_pd.h"
#include "usbc_ppc.h"

#if CONFIG_USB_PD_3A_PORTS != 1
#error Goroh reference must have at least one 3.0 A port
#endif

#define CPRINTS(format, args...) cprints(CC_USBPD, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_USBPD, format, ## args)

void svdm_set_hpd_gpio(int port, int en)
{
	if (port == 0) {
		gpio_set_level(GPIO_USB_C0_HPD_3V3, en);
	} else if (port == 1) {
		gpio_set_level(GPIO_USB_C1_HPD_3V3, en);
		gpio_set_level(GPIO_USB_C1_HPD_IN, en);
	}
}

int svdm_get_hpd_gpio(int port)
{
	if (port == 0)
		return gpio_get_level(GPIO_USB_C0_HPD_3V3);
	else
		return gpio_get_level(GPIO_USB_C1_HPD_3V3);
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

	/* Notify host of power info change. */
	pd_send_host_event(PD_EVENT_POWER_CHANGE);
}

int pd_check_vconn_swap(int port)
{
	/* Allow Vconn swap if AP is on. */
	return chipset_in_state(CHIPSET_STATE_SUSPEND | CHIPSET_STATE_ON);
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

	/* Notify host of power info change. */
	pd_send_host_event(PD_EVENT_POWER_CHANGE);

	return EC_SUCCESS;
}

int board_vbus_source_enabled(int port)
{
	return ppc_is_sourcing_vbus(port);
}
