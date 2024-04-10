/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "adc.h"
#include "atomic.h"
#include "baseboard_common.h"
#include "charge_manager.h"
#include "chipset.h"
#include "console.h"
#include "gpio.h"
#include "timer.h"
#include "usb_dp_alt_mode.h"
#include "usb_mux.h"
#include "usb_pd.h"
#include "usbc_ppc.h"

#if CONFIG_USB_PD_3A_PORTS != 1
#error Asurada reference must have at least one 3.0 A port
#endif

#define CPRINTS(format, args...) cprints(CC_USBPD, format, ##args)
#define CPRINTF(format, args...) cprintf(CC_USBPD, format, ##args)

int svdm_get_hpd_gpio(int port)
{
	/* HPD is low active, inverse the result */
	return !gpio_get_level(GPIO_EC_DPBRDG_HPD_ODL);
}

void svdm_set_hpd_gpio(int port, int en)
{
	/*
	 * HPD is low active, inverse the en
	 * TODO: C0&C1 shares the same HPD, implement FCFS policy.
	 */
	gpio_set_level(GPIO_EC_DPBRDG_HPD_ODL, !en);
}

/**
 * Is the port fine to be muxed its DisplayPort lines?
 *
 * Only one port can be muxed to DisplayPort at a time.
 *
 * @param port	Port number of TCPC.
 * @return	1 is fine; 0 is bad as other port is already muxed;
 */
static int is_dp_muxable(int port)
{
	int i;

	for (i = 0; i < board_get_usb_pd_port_count(); i++) {
		if (i != port) {
			if (usb_mux_get(i) & USB_PD_MUX_DP_ENABLED)
				return 0;
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

	if (!is_dp_muxable(port)) {
		/* TODO(waihong): Info user? */
		CPRINTS("p%d: The other port is already muxed.", port);
		return 0; /* nak */
	}

	if (lvl)
		gpio_set_level_verbose(CC_USBPD, GPIO_DP_AUX_PATH_SEL, port);

	if (chipset_in_state(CHIPSET_STATE_ANY_SUSPEND) && (irq || lvl))
		/*
		 * Wake up the AP.  IRQ or level high indicates a DP sink is now
		 * present.
		 */
		if (IS_ENABLED(CONFIG_MKBP_EVENT))
			pd_notify_dp_alt_mode_entry(port);

	/* Its initial DP status message prior to config */
	if (!(dp_flags[port] & DP_FLAGS_DP_ON)) {
		if (lvl)
			dp_flags[port] |= DP_FLAGS_HPD_HI_PENDING;
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
		if (now < svdm_hpd_deadline[port])
			crec_usleep(svdm_hpd_deadline[port] - now);

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
	if (port == USB_PD_PORT_TCPC_MST)
		baseboard_mst_enable_control(port, lvl);
#endif

	/* ack */
	return 1;
}

__override void svdm_exit_dp_mode(int port)
{
#ifdef CONFIG_USB_PD_DP_HPD_GPIO
	svdm_set_hpd_gpio(port, 0);
#endif /* CONFIG_USB_PD_DP_HPD_GPIO */
	usb_mux_hpd_update(port, USB_PD_MUX_HPD_LVL_DEASSERTED |
					 USB_PD_MUX_HPD_IRQ_DEASSERTED);

#ifdef USB_PD_PORT_TCPC_MST
	if (port == USB_PD_PORT_TCPC_MST)
		baseboard_mst_enable_control(port, 0);
#endif
}

int pd_snk_is_vbus_provided(int port)
{
	static atomic_t vbus_prev[CONFIG_USB_PD_PORT_MAX_COUNT];
	int vbus;

	if ((IS_ENABLED(BOARD_HAYATO) && board_get_version() < 4) ||
	    (IS_ENABLED(BOARD_SPHERION) && board_get_version() < 1))
		return ppc_is_vbus_present(port);

	/*
	 * (b:181203590#comment20) TODO(yllin): use
	 *  PD_VSINK_DISCONNECT_PD for non-5V case.
	 */
	vbus = adc_read_channel(board_get_vbus_adc(port)) >=
	       PD_V_SINK_DISCONNECT_MAX;

#ifdef CONFIG_USB_CHARGER
	/*
	 * There's no PPC to inform VBUS change for usb_charger, so inform
	 * the usb_charger now.
	 */
	if (!!(vbus_prev[port] != vbus))
		usb_charger_vbus_change(port, vbus);

	if (vbus)
		atomic_or(&vbus_prev[port], 1);
	else
		atomic_clear(&vbus_prev[port]);
#endif
	return vbus;
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

int pd_check_vconn_swap(int port)
{
	/* Allow Vconn swap if AP is on. */
	return chipset_in_state(CHIPSET_STATE_SUSPEND | CHIPSET_STATE_ON);
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

int board_vbus_source_enabled(int port)
{
	return ppc_is_sourcing_vbus(port);
}
