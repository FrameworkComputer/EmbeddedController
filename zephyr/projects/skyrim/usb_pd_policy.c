/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Shared USB-C policy for Zork boards */

#include <zephyr/drivers/gpio.h>

#include "charge_manager.h"
#include "chipset.h"
#include "common.h"
#include "compile_time_macros.h"
#include "console.h"
#include "ec_commands.h"
#include "ioexpander.h"
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
	return gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_pg_pwr_s5));
}

void pd_power_supply_reset(int port)
{
	/* Disable VBUS. */
	ppc_vbus_source_enable(port, 0);

	/* Enable discharge if we were previously sourcing 5V */
	if (IS_ENABLED(CONFIG_USB_PD_DISCHARGE))
		pd_set_vbus_discharge(port, 1);

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

__override int board_pd_set_frs_enable(int port, int enable)
{
	/*
	 * Both PPCs require the FRS GPIO to be set as soon as FRS capability
	 * is established.
	 */
	if (port == 0)
		ioex_set_level(IOEX_USB_C0_TCPC_FASTSW_CTL_EN, enable);
	else
		ioex_set_level(IOEX_USB_C1_TCPC_FASTSW_CTL_EN, enable);

	return EC_SUCCESS;
}

/* Used by Vbus discharge common code with CONFIG_USB_PD_DISCHARGE */
int board_vbus_source_enabled(int port)
{
	return tcpm_get_src_ctrl(port);
}

/* Used by USB charger task with CONFIG_USB_PD_5V_EN_CUSTOM */
int board_is_sourcing_vbus(int port)
{
	return board_vbus_source_enabled(port);
}
