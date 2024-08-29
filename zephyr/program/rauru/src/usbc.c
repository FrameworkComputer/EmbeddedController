/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "adc.h"
#include "charge_manager.h"
#include "chipset.h"
#include "console.h"
#include "hooks.h"
#include "usb_pd.h"
#include "usbc_ppc.h"
#include "zephyr_adc.h"

#define CPRINTS(format, args...) cprints(CC_USBCHARGE, format, ##args)

int board_set_active_charge_port(int port)
{
	int i;
	int is_valid_port = (port >= 0 && port < board_get_usb_pd_port_count());
	/* adjust the actual port count when not the type-c db connected. */

	if (!is_valid_port && port != CHARGE_PORT_NONE) {
		return EC_ERROR_INVAL;
	}

	if (port == CHARGE_PORT_NONE) {
		/* Disable all ports. */
		for (i = 0; i < board_get_usb_pd_port_count(); i++) {
			/*
			 * Do not return early if one fails otherwise we can
			 * get into a boot loop assertion failure.
			 */
			if (ppc_vbus_sink_enable(i, 0)) {
				CPRINTS("Disabling C%d as sink failed.", i);
			}
		}

		return EC_SUCCESS;
	}

	/* Check if the port is sourcing VBUS. */
	if (ppc_is_sourcing_vbus(port)) {
		CPRINTS("Skip enable C%d", port);
		return EC_ERROR_INVAL;
	}

	CPRINTS("New charge port: C%d", port);

	/*
	 * Turn off the other ports' sink path FETs, before enabling the
	 * requested charge port.
	 */
	for (i = 0; i < board_get_usb_pd_port_count(); i++) {
		if (i == port) {
			continue;
		}

		if (ppc_vbus_sink_enable(i, 0)) {
			CPRINTS("C%d: sink path disable failed.", i);
		}
	}

	/* Enable requested charge port. */
	if (ppc_vbus_sink_enable(port, 1)) {
		CPRINTS("C%d: sink path enable failed.", port);
		return EC_ERROR_UNKNOWN;
	}

	return EC_SUCCESS;
}

int board_vbus_source_enabled(int port)
{
	return ppc_is_sourcing_vbus(port);
}

static void notify_power_change(void)
{
	/* Notify host of power info change. */
	pd_send_host_event(PD_EVENT_POWER_CHANGE);
}
DECLARE_DEFERRED(notify_power_change);

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

	hook_call_deferred(&notify_power_change_data, 0);

	return EC_SUCCESS;
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

	/* defer pd_send_host_event to save ~2ms for PD compliance */
	hook_call_deferred(&notify_power_change_data, 0);
}

void board_reset_pd_mcu(void)
{
	/*
	 * C0 & C1: TCPC is embedded in the EC and processes interrupts in the
	 * chip code (it83xx/intc.c)
	 */
}

void board_pd_vconn_ctrl(int port, enum usbpd_cc_pin cc_pin, int enabled)
{
	/*
	 * We ignore the cc_pin and PPC vconn because polarity and PPC vconn
	 * should already be set correctly in the PPC driver via the pd
	 * state machine.
	 */
}

int pd_check_vconn_swap(int port)
{
	/* Allow Vconn swap if AP is on. */
	return chipset_in_state(CHIPSET_STATE_SUSPEND | CHIPSET_STATE_ON);
}

#ifdef CONFIG_USB_PD_VBUS_MEASURE_ADC_EACH_PORT
enum adc_channel board_get_vbus_adc(int port)
{
	if (port == 0) {
		return ADC_VBUS_C0;
	} else if (port == 1) {
		return ADC_VBUS_C1;
	}
	CPRINTS("Unknown vbus adc port id: %d", port);
	return ADC_VBUS_C0;
}
#endif /* CONFIG_USB_PD_VBUS_MEASURE_ADC_EACH_PORT */

__override int pd_snk_is_vbus_provided(int port)
{
	/*
	 * (b:181203590#comment20) TODO(yllin): use
	 *  PD_VSINK_DISCONNECT_PD for non-5V case.
	 */
	return adc_read_channel(board_get_vbus_adc(port)) >=
	       PD_V_SINK_DISCONNECT_MAX;
}
