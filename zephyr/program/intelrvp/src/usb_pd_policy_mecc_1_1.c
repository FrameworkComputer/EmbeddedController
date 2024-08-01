/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "console.h"
#include "gpio.h"
#include "intelrvp.h"
#include "usb_mux.h"
#include "usbc_ppc.h"

#define CPRINTF(format, args...) cprintf(CC_USBPD, format, ##args)
#define CPRINTS(format, args...) cprints(CC_USBPD, format, ##args)

static inline void board_pd_set_vbus_discharge(int port, bool enable)
{
	if (board_port_has_ppc(port)) {
		ppc_discharge_vbus(port, enable);
	} else {
		tcpc_discharge_vbus(port, enable);
	}
}

int pd_set_power_supply_ready(int port)
{
	int rv;

	/* Disable charging. */
	if (board_port_has_ppc(port)) {
		rv = ppc_vbus_sink_enable(port, 0);
	} else {
		rv = tcpc_config[port].drv->set_snk_ctrl(port, 0);
	}

	if (rv) {
		return rv;
	}

	board_pd_set_vbus_discharge(port, false);

	/* Provide Vbus. */
	if (board_port_has_ppc(port)) {
		rv = ppc_vbus_source_enable(port, 1);
	} else {
		tcpc_config[port].drv->set_src_ctrl(port, 1);
	}

	if (rv) {
		return rv;
	}

	/* Notify host of power info change. */
	pd_send_host_event(PD_EVENT_POWER_CHANGE);

	return EC_SUCCESS;
}

void pd_power_supply_reset(int port)
{
	int prev_en;

	prev_en = board_vbus_source_enabled(port);

	/* Disable VBUS. */
	if (board_port_has_ppc(port)) {
		ppc_vbus_source_enable(port, 0);
	} else {
		tcpc_config[port].drv->set_src_ctrl(port, 0);
	}

	/* Enable discharge if we were previously sourcing 5V */
	if (prev_en) {
		board_pd_set_vbus_discharge(port, true);
	}

	/* Notify host of power info change. */
	pd_send_host_event(PD_EVENT_POWER_CHANGE);
}

int pd_check_vconn_swap(int port)
{
	/* Only allow vconn swap if PP3300 rail is enabled */
	return gpio_get_level(GPIO_EN_PP3300_A);
}

test_mockable int pd_snk_is_vbus_provided(int port)
{
	if (board_port_has_ppc(port)) {
		return ppc_is_vbus_present(port);
	} else {
		return tcpc_config[port].drv->check_vbus_level(port,
							       VBUS_PRESENT);
	}
}

int board_vbus_source_enabled(int port)
{
	if (is_typec_port(port)) {
		if (board_port_has_ppc(port)) {
			return ppc_is_sourcing_vbus(port);
		} else {
			return tcpc_config[port].drv->get_src_ctrl(port);
		}
	}
	return 0;
}
