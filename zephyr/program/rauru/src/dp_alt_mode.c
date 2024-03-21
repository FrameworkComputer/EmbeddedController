/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "chipset.h"
#include "console.h"
#include "hooks.h"
#include "timer.h"
#include "typec_control.h"
#include "usb_mux.h"
#include "usb_pd.h"

#define CPRINTS(format, args...) cprints(CC_USBPD, format, ##args)
#define CPRINTF(format, args...) cprintf(CC_USBPD, format, ##args)

enum dp_port {
	DP_PORT_NONE = -1,
	DP_PORT_C0 = 0,
	DP_PORT_C1,
	DP_PORT_HDMI,
};

static int active_dp_port = DP_PORT_NONE;

static void set_dp_path_sel(int port)
{
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_dp_path_usb_c1_en), port);
	CPRINTS("Set DP path C%d", port);
}

int svdm_get_hpd_gpio(int port)
{
	/* HPD is low active, inverse the result */
	return !gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_ap_dp_hpd_l));
}

static void reset_aux_deferred(void)
{
	if (active_dp_port == DP_PORT_NONE)
		/* reset to 1 for lower power consumption. */
		set_dp_path_sel(1);
}
DECLARE_DEFERRED(reset_aux_deferred);

void svdm_set_hpd_gpio(int port, int en)
{
	/*
	 * HPD is low active, inverse the en.
	 *
	 * Implement FCFS policy:
	 * 1) Enable hpd if no active port.
	 * 2) Disable hpd if active port is the given port.
	 */
	if (en && active_dp_port < 0) {
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_ap_dp_hpd_l), 0);
		active_dp_port = port;
		hook_call_deferred(&reset_aux_deferred_data, -1);
	}

	if (!en && active_dp_port == port) {
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_ap_dp_hpd_l), 1);
		active_dp_port = DP_PORT_NONE;
		/*
		 * This might be a HPD debounce to send a HPD IRQ (500us), so
		 * do not reset the aux path immediately. Defer this call and
		 * re-check if this is a real disable.
		 */
		hook_call_deferred(&reset_aux_deferred_data, 1 * MSEC);
	}
}

__override void svdm_dp_post_config(int port)
{
	mux_state_t mux_mode = svdm_dp_get_mux_mode(port);

	typec_set_sbu(port, true);

	/*
	 * Prior to post-config, the mux will be reset to safe mode, and this
	 * will break mux config and aux path config we did in the first DP
	 * status command. Only enable this if the port is the current aux-port.
	 */
	if (port == active_dp_port) {
		usb_mux_set(port, mux_mode, USB_SWITCH_CONNECT,
			    polarity_rm_dts(pd_get_polarity(port)));
		usb_mux_hpd_update(port, USB_PD_MUX_HPD_LVL |
						 USB_PD_MUX_HPD_IRQ_DEASSERTED);
	} else {
		usb_mux_set(port, mux_mode & USB_PD_MUX_USB_ENABLED,
			    USB_SWITCH_CONNECT,
			    polarity_rm_dts(pd_get_polarity(port)));
	}

	dp_flags[port] |= DP_FLAGS_DP_ON;
}

static int is_dp_muxable(enum dp_port port)
{
	int i;
	/* TODO: query anx7510 GPIO1 instead. */
	int hdmi_in_use = false;

	/* check HDMI HPD event */
	if (hdmi_in_use && port != DP_PORT_HDMI) {
		return 0;
	}

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
	mux_state_t mux_state, mux_mode;

	mux_mode = svdm_dp_get_mux_mode(port);
	dp_status[port] = payload[1];

	if (!is_dp_muxable(port)) {
		/* TODO(waihong): Info user? */
		CPRINTS("p%d: The other port is already muxed.", port);
		return 0; /* nak */
	}

	if (lvl) {
		set_dp_path_sel(port);

		usb_mux_set(port, mux_mode, USB_SWITCH_CONNECT,
			    polarity_rm_dts(pd_get_polarity(port)));
	} else {
		usb_mux_set(port, USB_PD_MUX_USB_ENABLED, USB_SWITCH_CONNECT,
			    polarity_rm_dts(pd_get_polarity(port)));
	}

	if (chipset_in_state(CHIPSET_STATE_ANY_SUSPEND) && (irq || lvl)) {
		/*
		 * Wake up the AP.  IRQ or level high indicates a DP sink is now
		 * present.
		 */
		if (IS_ENABLED(CONFIG_MKBP_EVENT)) {
			pd_notify_dp_alt_mode_entry(port);
		}
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
