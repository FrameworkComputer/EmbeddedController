/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "chipset.h"
#include "console.h"
#include "timer.h"
#include "typec_control.h"
#include "usb_mux.h"
#include "usb_pd.h"
#include "usb_pd_dp_hpd_gpio.h"
#include "usbc/pdc_power_mgmt.h"

#include <stdint.h>

#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(skywalker_usbc, LOG_LEVEL_INF);

uint64_t svdm_hpd_deadline[CONFIG_USB_PD_PORT_MAX_COUNT];
static int active_aux_port = -1;

int svdm_get_hpd_gpio(int port)
{
	if (active_aux_port != port) {
		return 0;
	}

	return gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_ap_dp_hpd_l));
}

static void set_dp_path_sel(int port)
{
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_dp_aux_path_sel), port);
	LOG_INF("Set DP_AUX_PATH_SEL: %d", port);
}

void svdm_set_hpd_gpio(int port, int en)
{
	/*
	 * Implement FCFS policy:
	 * 1) Enable hpd if no active port.
	 * 2) Disable hpd if active port is the given port.
	 */
	if (en && active_aux_port < 0) {
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_ap_dp_hpd_l), 1);
		active_aux_port = port;
	}

	if (!en && active_aux_port == port) {
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_ap_dp_hpd_l), 0);
		active_aux_port = -1;
	}
}

static bool is_dp_muxable(int port)
{
	for (int i = 0; i < board_get_usb_pd_port_count(); i++) {
		if (i != port) {
			if (usb_mux_get(i) & USB_PD_MUX_DP_ENABLED) {
				return false;
			}
		}
	}

	return true;
}

static void skywalker_dp_attention(int port, uint32_t vdo_dp_status)
{
	int lvl = PD_VDO_DPSTS_HPD_LVL(vdo_dp_status);
	int irq = PD_VDO_DPSTS_HPD_IRQ(vdo_dp_status);

	if (!is_dp_muxable(port)) {
		LOG_INF("p%d: The other port is already muxed.", port);
		return;
	}

	int cur_lvl = svdm_get_hpd_gpio(port);
	mux_state_t mux_mode = pdc_power_mgmt_get_dp_mux_mode(port);

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
		pd_notify_dp_alt_mode_entry(port);
	}

	if (irq && !lvl) {
		/*
		 * IRQ can only be generated when the level is high, because
		 * the IRQ is signaled by a short low pulse from the high level.
		 */
		LOG_ERR("ERR:HPD:IRQ&LOW\n");
		return;
	}

	if (irq && cur_lvl) {
		uint64_t now = get_time().val;

		/* wait for the minimum spacing between IRQ_HPD if needed */
		if (now < svdm_hpd_deadline[port]) {
			k_usleep(svdm_hpd_deadline[port] - now);
		}

		/* generate IRQ_HPD pulse */
		svdm_set_hpd_gpio(port, 0);
		/*
		 * b/171172053#comment14: since the HPD_DSTREAM_DEBOUNCE_IRQ is
		 * very short (500us), we can use k_busy_wait for more stable
		 * pulse period.
		 */
		k_busy_wait(HPD_DSTREAM_DEBOUNCE_IRQ);
		svdm_set_hpd_gpio(port, 1);
	} else {
		svdm_set_hpd_gpio(port, lvl);
	}

	/* set the minimum time delay (2ms) for the next HPD IRQ */
	svdm_hpd_deadline[port] = get_time().val + HPD_USTREAM_DEBOUNCE_LVL;

	/*
	 * Populate MUX state before dp path mux, so we can keep the HPD status.
	 */
	mux_mode = (lvl ? USB_PD_MUX_HPD_LVL : USB_PD_MUX_HPD_LVL_DEASSERTED) |
		   (irq ? USB_PD_MUX_HPD_IRQ : USB_PD_MUX_HPD_IRQ_DEASSERTED);
	usb_mux_hpd_update(port, mux_mode);

	return;
}

static void skywalker_set_unattached(int port)
{
	svdm_set_hpd_gpio(port, 0);
}

static int skywalker_pdc_cb_init(void)
{
	pdc_power_mgmt_register_board_callback(PDC_BOARD_CB_UNATTACH,
					       skywalker_set_unattached);
	pdc_power_mgmt_register_board_callback(PDC_BOARD_CB_DP_ATTENTION,
					       skywalker_dp_attention);
	return 0;
}
SYS_INIT(skywalker_pdc_cb_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
