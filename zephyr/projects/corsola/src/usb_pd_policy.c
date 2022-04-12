/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "atomic.h"
#include "console.h"
#include "chipset.h"
#include "timer.h"
#include "usb_dp_alt_mode.h"
#include "usb_mux.h"
#include "usb_pd.h"
#include "usbc_ppc.h"

#include "baseboard_usbc_config.h"

#if CONFIG_USB_PD_3A_PORTS != 1
#error Corsola reference must have at least one 3.0 A port
#endif

#define CPRINTS(format, args...) cprints(CC_USBPD, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_USBPD, format, ## args)

int pd_check_vconn_swap(int port)
{
	/* Allow Vconn swap if AP is on. */
	return chipset_in_state(CHIPSET_STATE_SUSPEND | CHIPSET_STATE_ON);
}

static void set_dp_aux_path_sel(int port)
{
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(dp_aux_path_sel), port);
	CPRINTS("Set DP_AUX_PATH_SEL: %d", port);
}

int svdm_get_hpd_gpio(int port)
{
	/* HPD is low active, inverse the result */
	return !gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(ec_ap_dp_hpd_odl));
}

void svdm_set_hpd_gpio(int port, int en)
{
	static int active_port = -1;

	/*
	 * HPD is low active, inverse the en.
	 *
	 * Implement FCFS policy:
	 * 1) Enable hpd if no active port.
	 * 2) Disable hpd if active port is the given port.
	 */
	if (en && active_port < 0) {
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(ec_ap_dp_hpd_odl), 0);
		active_port = port;
	}

	if (!en && active_port == port) {
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(ec_ap_dp_hpd_odl), 1);
		active_port = -1;
		set_dp_aux_path_sel(0);
	}
}

__override int svdm_dp_config(int port, uint32_t *payload)
{
	int opos = pd_alt_mode(port, TCPCI_MSG_SOP, USB_SID_DISPLAYPORT);
	uint8_t pin_mode = get_dp_pin_mode(port);
	mux_state_t mux_mode = svdm_dp_get_mux_mode(port);
	int mf_pref = PD_VDO_DPSTS_MF_PREF(dp_status[port]);

	if (!pin_mode) {
		return 0;
	}

	CPRINTS("pin_mode: %x, mf: %d, mux: %d", pin_mode, mf_pref, mux_mode);
	/*
	 * Defer setting the usb_mux until HPD goes high, svdm_dp_attention().
	 * The AP only supports one DP phy. An external DP mux switches between
	 * the two ports. Should switch those muxes when it is really used,
	 * i.e. HPD high; otherwise, the real use case is preempted, like:
	 *  (1) plug a dongle without monitor connected to port-0,
	 *  (2) plug a dongle without monitor connected to port-1,
	 *  (3) plug a monitor to the port-1 dongle.
	 */

	payload[0] = VDO(USB_SID_DISPLAYPORT, 1,
			 CMD_DP_CONFIG | VDO_OPOS(opos));
	payload[1] = VDO_DP_CFG(pin_mode,      /* pin mode */
				1,             /* DPv1.3 signaling */
				2);            /* UFP connected */
	return 2;
};

__override void svdm_dp_post_config(int port)
{
	dp_flags[port] |= DP_FLAGS_DP_ON;
}

int corsola_is_dp_muxable(int port)
{
	int i;

	for (i = 0; i < board_get_usb_pd_port_count(); i++) {
		if (i != port) {
			if (usb_mux_get(i) & USB_PD_MUX_DP_ENABLED) {
				return 0;
			}
		}
	}

	return 1;
}

__override int svdm_dp_attention(int port, uint32_t *payload)
{
	int lvl = PD_VDO_DPSTS_HPD_LVL(payload[1]);
	int irq = PD_VDO_DPSTS_HPD_IRQ(payload[1]);
#ifdef CONFIG_USB_PD_DP_HPD_GPIO
	int cur_lvl = svdm_get_hpd_gpio(port);
#endif /* CONFIG_USB_PD_DP_HPD_GPIO */
	mux_state_t mux_state;

	dp_status[port] = payload[1];

	if (!corsola_is_dp_muxable(port)) {
		/* TODO(waihong): Info user? */
		CPRINTS("p%d: The other port is already muxed.", port);
		return 0; /* nak */
	}

	if (lvl) {
		set_dp_aux_path_sel(port);

		usb_mux_set(port, USB_PD_MUX_DOCK,
			    USB_SWITCH_CONNECT,
			    polarity_rm_dts(pd_get_polarity(port)));
	} else {
		usb_mux_set(port, USB_PD_MUX_USB_ENABLED,
			    USB_SWITCH_CONNECT,
			    polarity_rm_dts(pd_get_polarity(port)));
	}

	if (chipset_in_state(CHIPSET_STATE_ANY_SUSPEND) &&
	    (irq || lvl)) {
		/*
		 * Wake up the AP.  IRQ or level high indicates a DP sink is now
		 * present.
		 */
		if (IS_ENABLED(CONFIG_MKBP_EVENT)) {
			pd_notify_dp_alt_mode_entry(port);
		}
	}

	/* Its initial DP status message prior to config */
	if (!(dp_flags[port] & DP_FLAGS_DP_ON)) {
		if (lvl) {
			dp_flags[port] |= DP_FLAGS_HPD_HI_PENDING;
		}
		return 1;
	}

#ifdef CONFIG_USB_PD_DP_HPD_GPIO
	if (irq && !lvl) {
		/*
		 * IRQ can only be generated when the level is high, because
		 * the IRQ is signaled by a short low pulse from the high level.
		 */
		CPRINTF("ERR:HPD:IRQ&LOW\n");
		return 0; /* nak */
	}

	if (irq && cur_lvl) {
		uint64_t now = get_time().val;
		/* wait for the minimum spacing between IRQ_HPD if needed */
		if (now < svdm_hpd_deadline[port]) {
			usleep(svdm_hpd_deadline[port] - now);
		}

		/* generate IRQ_HPD pulse */
		svdm_set_hpd_gpio(port, 0);
		/*
		 * b/171172053#comment14: since the HPD_DSTREAM_DEBOUNCE_IRQ is
		 * very short (500us), we can use udelay instead of usleep for
		 * more stable pulse period.
		 */
		udelay(HPD_DSTREAM_DEBOUNCE_IRQ);
		svdm_set_hpd_gpio(port, 1);
	} else {
		svdm_set_hpd_gpio(port, lvl);
	}

	/* set the minimum time delay (2ms) for the next HPD IRQ */
	svdm_hpd_deadline[port] = get_time().val + HPD_USTREAM_DEBOUNCE_LVL;
#endif /* CONFIG_USB_PD_DP_HPD_GPIO */

	mux_state = (lvl ? USB_PD_MUX_HPD_LVL : USB_PD_MUX_HPD_LVL_DEASSERTED) |
		    (irq ? USB_PD_MUX_HPD_IRQ : USB_PD_MUX_HPD_IRQ_DEASSERTED);
	usb_mux_hpd_update(port, mux_state);

#ifdef USB_PD_PORT_TCPC_MST
	if (port == USB_PD_PORT_TCPC_MST) {
		baseboard_mst_enable_control(port, lvl);
	}
#endif

	/* ack */
	return 1;
}
