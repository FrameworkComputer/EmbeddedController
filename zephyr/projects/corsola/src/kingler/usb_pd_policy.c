/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "charge_manager.h"
#include "console.h"
#include "driver/ppc/rt1718s.h"
#include "system.h"
#include "usb_mux.h"
#include "usb_pd.h"
#include "usbc_ppc.h"
#include "util.h"

#include "baseboard_usbc_config.h"

#define CPRINTS(format, args...) cprints(CC_USBCHARGE, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_USBCHARGE, format, ## args)

void pd_power_supply_reset(int port)
{
	int prev_en;

	prev_en = ppc_is_sourcing_vbus(port);

	if (port == USBC_PORT_C1) {
		rt1718s_gpio_set_level(port, GPIO_EN_USB_C1_SOURCE, 0);
	}

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
	if (port == USBC_PORT_C1) {
		rt1718s_gpio_set_level(port, GPIO_EN_USB_C1_SOURCE, 1);
	}

	rv = ppc_vbus_source_enable(port, 1);
	if (rv) {
		return rv;
	}

	/* Notify host of power info change. */
	pd_send_host_event(PD_EVENT_POWER_CHANGE);

	return EC_SUCCESS;
}

int pd_snk_is_vbus_provided(int port)
{
	/* TODO: use ADC? */
	return tcpm_check_vbus_level(port, VBUS_PRESENT);
}
