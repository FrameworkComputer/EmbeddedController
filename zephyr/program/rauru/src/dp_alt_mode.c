/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "chipset.h"
#include "console.h"
#include "hooks.h"
#include "rauru_dp.h"
#include "timer.h"
#include "typec_control.h"
#include "usb_mux.h"
#include "usb_pd.h"
#include "usb_pd_dp_hpd_gpio.h"

#define CPRINTS(format, args...) cprints(CC_USBPD, format, ##args)
#define CPRINTF(format, args...) cprintf(CC_USBPD, format, ##args)

static int active_dp_port = DP_PORT_NONE;

bool rauru_is_hpd_high(enum rauru_dp_port port)
{
#if CONFIG_RAURU_BOARD_HAS_HDMI_SUPPORT
	if (port == DP_PORT_HDMI) {
		return gpio_pin_get_dt(
			GPIO_DT_FROM_NODELABEL(gpio_hdmi_ec_hpd));
	}
#endif

	return PD_VDO_DPSTS_HPD_LVL(dp_status[port]);
}

enum rauru_dp_port rauru_get_dp_path(void)
{
	return active_dp_port;
}

void rauru_detach_dp_path(enum rauru_dp_port port)
{
	if (port != active_dp_port) {
		return;
	}

	/* Detach and then rotate. Priority: HDMI -> C0 -> C1 */
#if CONFIG_RAURU_BOARD_HAS_HDMI_SUPPORT
	if (port != DP_PORT_HDMI && rauru_is_hpd_high(DP_PORT_HDMI)) {
		rauru_set_dp_path(DP_PORT_HDMI);
		return;
	}
#endif

	for (int i = 0; i < board_get_usb_pd_port_count(); i++) {
		if (i != port && rauru_is_hpd_high(i)) {
			/* TODO(yllin): should set IRQ_HPD as well? */
			rauru_set_dp_path(i);
			return;
		}
	}

	/* no other active port,  */
	rauru_set_dp_path(DP_PORT_NONE);
}

void rauru_set_dp_path(enum rauru_dp_port port)
{
	const struct gpio_dt_spec *c1_en =
		GPIO_DT_FROM_NODELABEL(gpio_dp_path_usb_c1_en);
	const struct gpio_dt_spec *hdmi_en =
		GPIO_DT_FROM_NODELABEL(gpio_dp_path_hdmi_en);

	if (port == active_dp_port) {
		return;
	}

	/*
	 * DP Pipe -> DPMux -> C1
	 *              |----> DP Mux -> HDMI
	 *                       |-----> C0
	 * Also, set EN pin to LOW for power saving when unused.
	 */
	if (port == DP_PORT_C1) {
		gpio_pin_set_dt(c1_en, 1);
		gpio_pin_set_dt(hdmi_en, 0);
	} else {
		gpio_pin_set_dt(c1_en, 0);
		if (port == DP_PORT_HDMI) {
			gpio_pin_set_dt(hdmi_en, 1);
		} else {
			/* C0, None, and other invalid ports */
			gpio_pin_set_dt(hdmi_en, 0);
		}
	}
	active_dp_port = port;
	CPRINTS("DP p%d", port);
}

int svdm_get_hpd_gpio(int port)
{
	/* HPD is low active, inverse the result */
	return !gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_ap_dp_hpd_l));
}

void svdm_set_hpd_gpio(int port, int en)
{
	if (port != active_dp_port)
		return;

	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_ap_dp_hpd_l), !en);
}

__override void svdm_dp_post_config(int port)
{
	mux_state_t mux_mode = svdm_dp_get_mux_mode(port);

	typec_set_sbu(port, true);

	dp_flags[port] |= DP_FLAGS_DP_ON;

	if (port == active_dp_port) {
		usb_mux_set(port, mux_mode, USB_SWITCH_CONNECT,
			    polarity_rm_dts(pd_get_polarity(port)));
		usb_mux_hpd_update(port, USB_PD_MUX_HPD_LVL |
						 USB_PD_MUX_HPD_IRQ_DEASSERTED);
	} else {
		usb_mux_set(port, mux_mode & (~USB_PD_MUX_DP_ENABLED),
			    USB_SWITCH_CONNECT,
			    polarity_rm_dts(pd_get_polarity(port)));
	}
}

int rauru_is_dp_muxable(enum rauru_dp_port port)
{
	return port == active_dp_port || port == DP_PORT_NONE;
}

__override int svdm_dp_attention(int port, uint32_t *payload)
{
	int lvl = PD_VDO_DPSTS_HPD_LVL(payload[1]);
	int irq = PD_VDO_DPSTS_HPD_IRQ(payload[1]);
	mux_state_t mux_state, mux_mode;

	mux_mode = svdm_dp_get_mux_mode(port);
	dp_status[port] = payload[1];

	if (!rauru_is_dp_muxable(port)) {
		/* TODO(waihong): Info user? */
		CPRINTS("p%d: The other port is already muxed.", port);
		return 0; /* nak */
	}

	if (lvl) {
		rauru_set_dp_path(port);
		usb_mux_set(port, mux_mode, USB_SWITCH_CONNECT,
			    polarity_rm_dts(pd_get_polarity(port)));
	} else {
		rauru_detach_dp_path(port);
		usb_mux_set(port, mux_mode & (~USB_PD_MUX_USB_ENABLED),
			    USB_SWITCH_CONNECT,
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

	if (dp_hpd_gpio_set(port, lvl, irq) != EC_SUCCESS) {
		return 0;
	}

	/*
	 * Populate MUX state before dp path mux, so we can keep the HPD status.
	 */
	mux_state = (lvl ? USB_PD_MUX_HPD_LVL : USB_PD_MUX_HPD_LVL_DEASSERTED) |
		    (irq ? USB_PD_MUX_HPD_IRQ : USB_PD_MUX_HPD_IRQ_DEASSERTED);
	usb_mux_hpd_update(port, mux_state);

	/* ack */
	return 1;
}

__overridable void svdm_exit_dp_mode(int port)
{
	dp_flags[port] = 0;
	dp_status[port] = 0;
	dp_hpd_gpio_set(port, false, false);
	usb_mux_hpd_update(port, USB_PD_MUX_HPD_LVL_DEASSERTED |
					 USB_PD_MUX_HPD_IRQ_DEASSERTED);
	rauru_detach_dp_path(port);
}
