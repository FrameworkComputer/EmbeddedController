/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "adc.h"
#include "board.h"
#include "charge_state.h"
#include "chipset.h"
#include "driver/charger/sm5803.h"
#include "driver/tcpm/it83xx_pd.h"
#include "driver/tcpm/ps8xxx_public.h"
#include "driver/tcpm/tcpci.h"
#include "extpower.h"
#include "gpio/gpio_int.h"
#include "hooks.h"
#include "system.h"
#include "usb_mux.h"
#include "usb_pd.h"
#include "usbc_ppc.h"

#include <zephyr/logging/log.h>

#include <ap_power/ap_power.h>

LOG_MODULE_DECLARE(nissa, CONFIG_NISSA_LOG_LEVEL);

/*
 * Enable interrupts
 */
static void board_init(void)
{
	/*
	 * Enable USB-C interrupts.
	 */
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_usb_c0));
}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_DEFAULT);

__override int extpower_is_present(void)
{
	/*
	 * There's no battery, so running this method implies we have power.
	 */
	return 1;
}

int board_vbus_source_enabled(int port)
{
	if (port != CHARGE_PORT_TYPEC0)
		return 0;

	return ppc_is_sourcing_vbus(port);
}

/* Vconn control for integrated ITE TCPC */
void board_pd_vconn_ctrl(int port, enum usbpd_cc_pin cc_pin, int enabled)
{
	/*
	 * We ignore the cc_pin and PPC vconn because polarity and PPC vconn
	 * should already be set correctly in the PPC driver via the pd
	 * state machine.
	 */
}

void pd_power_supply_reset(int port)
{
	int prev_en;

	if (port < 0 || port >= board_get_usb_pd_port_count())
		return;

	prev_en = ppc_is_sourcing_vbus(port);

	/* Disable VBUS source */
	ppc_vbus_source_enable(port, 0);

	/* Enable discharge if we were previously sourcing 5V */
	if (prev_en)
		pd_set_vbus_discharge(port, 1);

	/* Notify host of power info change. */
	pd_send_host_event(PD_EVENT_POWER_CHANGE);
}

int pd_set_power_supply_ready(int port)
{
	int rv;

	if (port < 0 || port > board_get_usb_pd_port_count()) {
		LOG_WRN("Port C%d does not exist, cannot enable VBUS", port);
		return EC_ERROR_INVAL;
	}

	/* Disable charging */
	rv = ppc_vbus_sink_enable(port, 0);
	if (rv)
		return rv;

	pd_set_vbus_discharge(port, 0);

	/* Enable VBUS source */
	rv = ppc_vbus_source_enable(port, 1);
	if (rv)
		return rv;

	/* Notify host of power info change. */
	pd_send_host_event(PD_EVENT_POWER_CHANGE);

	return EC_SUCCESS;
}

/* LCOV_EXCL_START function does nothing, but is required for build */
void board_reset_pd_mcu(void)
{
	/*
	 * Nothing to do.  TCPC C0 is internal.
	 */
}
/* LCOV_EXCL_STOP */

/*
 * Handle charger interrupts in the PD task. Not doing so can lead to a priority
 * inversion where we fail to respond to TCPC alerts quickly enough because we
 * don't get another edge on a shared IRQ until the other interrupt is cleared
 * (or the IRQ is polled again), which happens in lower-priority tasks: the
 * high-priority type-C handler is thus blocked on the lower-priority one(s).
 *
 * To avoid that, we run charger and BC1.2 interrupts synchronously alongside
 * PD interrupts so they have the same priority.
 */
void board_process_pd_alert(int port)
{
	/*
	 * Port 0 doesn't use an external TCPC, so its interrupts don't need
	 * this special handling.
	 */
	return;
}

int pd_snk_is_vbus_provided(int port)
{
	if (port != CHARGE_PORT_TYPEC0)
		return 0;

	return ppc_is_vbus_present(port);
}

__override int board_get_vbus_voltage(int port)
{
	return adc_read_channel(ADC_VBUS);
}
