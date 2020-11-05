/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "console.h"
#include "driver/tcpm/tcpci.h"
#include "usb_pd.h"
#include "usbc_ppc.h"

#define CPRINTF(format, args...) cprintf(CC_USBPD, format, ## args)
#define CPRINTS(format, args...) cprints(CC_USBPD, format, ## args)

/*
 * TODO(b/167711550): These 4 functions need to be implemented for honeybuns
 * and are required to build with TCPMv2 enabled. Currently, they only allow the
 * build to work. They will be implemented in a subsequent CL.
 */

int pd_check_vconn_swap(int port)
{
	/*TODO: Dock is the Vconn source */
	return 1;
}

void pd_power_supply_reset(int port)
{
	int prev_en;

	if (port < 0 || port >= CONFIG_USB_PD_PORT_MAX_COUNT)
		return;

	prev_en = ppc_is_sourcing_vbus(port);

	/* Disable VBUS. */
	ppc_vbus_source_enable(port, 0);

	/* Enable discharge if we were previously sourcing 5V */
	if (prev_en)
		pd_set_vbus_discharge(port, 1);
}

int pd_set_power_supply_ready(int port)
{
	int rv;

	/*
	 * Default operation of buck-boost is 5v/3.6A.
	 * Turn on the PPC Provide Vbus.
	 */
	rv = ppc_vbus_source_enable(port, 1);
	if (rv)
		return rv;

	return EC_SUCCESS;
}

int pd_snk_is_vbus_provided(int port)
{
	return ppc_is_vbus_present(port);
}

int board_vbus_source_enabled(int port)
{
	return ppc_is_sourcing_vbus(port);
}

void pd_set_input_current_limit(int port, uint32_t max_ma,
				uint32_t supply_voltage)
{

}
