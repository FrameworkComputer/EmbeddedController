/* Copyright 2018 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Shared USB-C policy for Grunt boards */

#include "charge_manager.h"
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

#define CPRINTF(format, args...) cprintf(CC_USBPD, format, ##args)
#define CPRINTS(format, args...) cprints(CC_USBPD, format, ##args)

#define PDO_FIXED_FLAGS \
	(PDO_FIXED_DUAL_ROLE | PDO_FIXED_DATA_SWAP | PDO_FIXED_COMM_CAP)

int pd_check_vconn_swap(int port)
{
	/* in G3, do not allow vconn swap since 5V rail is off */
	return gpio_get_level(GPIO_S5_PGOOD);
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

int pd_snk_is_vbus_provided(int port)
{
	return ppc_is_vbus_present(port);
}

int board_vbus_source_enabled(int port)
{
	return ppc_is_sourcing_vbus(port);
}

/* ----------------- Vendor Defined Messages ------------------ */
#ifdef CONFIG_USB_PD_ALT_MODE_DFP
mux_state_t svdm_dp_mux_mode(int port)
{
	int mf_pref = PD_VDO_DPSTS_MF_PREF(dp_status[port]);
	int pin_mode = pd_dfp_dp_get_pin_mode(port, dp_status[port]);
	/*
	 * Multi-function operation is only allowed if that pin config is
	 * supported.
	 */
	if ((pin_mode & MODE_DP_PIN_MF_MASK) && mf_pref)
		return USB_PD_MUX_DOCK;
	else
		return USB_PD_MUX_DP_ENABLED;
}

__override int svdm_dp_config(int port, uint32_t *payload)
{
	int opos = pd_alt_mode(port, TCPCI_MSG_SOP, USB_SID_DISPLAYPORT);
	int mf_pref = PD_VDO_DPSTS_MF_PREF(dp_status[port]);
	int pin_mode = pd_dfp_dp_get_pin_mode(port, dp_status[port]);
	mux_state_t mux_mode = svdm_dp_mux_mode(port);

	if (!pin_mode)
		return 0;

	CPRINTS("pin_mode: %x, mf: %d, mux: %d", pin_mode, mf_pref, mux_mode);

	/*
	 * Place the USB Type-C pins that are to be re-configured to DisplayPort
	 * Configuration into the Safe state. For USB_PD_MUX_DOCK, the
	 * superspeed signals can remain connected. For USB_PD_MUX_DP_ENABLED,
	 * disconnect the superspeed signals here, before the pins are
	 * re-configured to DisplayPort (in svdm_dp_post_config, when we receive
	 * the config ack).
	 */
	if (mux_mode == USB_PD_MUX_DP_ENABLED)
		usb_mux_set(port, USB_PD_MUX_NONE, USB_SWITCH_CONNECT,
			    polarity_rm_dts(pd_get_polarity(port)));

	payload[0] =
		VDO(USB_SID_DISPLAYPORT, 1, CMD_DP_CONFIG | VDO_OPOS(opos));
	payload[1] = VDO_DP_CFG(pin_mode, /* pin mode */
				1, /* DPv1.3 signaling */
				2); /* UFP connected */
	return 2;
};

#define PORT_TO_HPD(port) ((port) ? GPIO_USB_C1_DP_HPD : GPIO_USB_C0_DP_HPD)
__override void svdm_dp_post_config(int port)
{
	/* Connect the SBU and USB lines to the connector. */
	ppc_set_sbu(port, 1);
	usb_mux_set(port, svdm_dp_mux_mode(port), USB_SWITCH_CONNECT,
		    polarity_rm_dts(pd_get_polarity(port)));

	dp_flags[port] |= DP_FLAGS_DP_ON;
	if (!(dp_flags[port] & DP_FLAGS_HPD_HI_PENDING))
		return;

	gpio_set_level(PORT_TO_HPD(port), 1);

	/* set the minimum time delay (2ms) for the next HPD IRQ */
	svdm_hpd_deadline[port] = get_time().val + HPD_USTREAM_DEBOUNCE_LVL;
	usb_mux_hpd_update(port,
			   USB_PD_MUX_HPD_LVL | USB_PD_MUX_HPD_IRQ_DEASSERTED);
}

#endif /* CONFIG_USB_PD_ALT_MODE_DFP */
