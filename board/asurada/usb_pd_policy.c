/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "charge_manager.h"
#include "chipset.h"
#include "timer.h"
#include "usb_dp_alt_mode.h"
#include "usb_mux.h"
#include "usb_pd.h"
#include "usbc_ppc.h"

#ifndef CONFIG_USB_PD_MAX_SINGLE_SOURCE_CURRENT
#error Asurada reference must define CONFIG_USB_PD_MAX_SINGLE_SOURCE_CURRENT
#endif

#define CPRINTS(format, args...) cprints(CC_USBPD, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_USBPD, format, ## args)

/* The port that the aux channel is on. */
static enum {
	AUX_PORT_NONE = -1,
	AUX_PORT_C0 = 0,
	AUX_PORT_C1HDMI = 1,
} aux_port = AUX_PORT_NONE;

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

static void aux_switch_port(int port)
{
	if (port != AUX_PORT_NONE)
		gpio_set_level_verbose(CC_USBPD, GPIO_DP_AUX_PATH_SEL, port);
	aux_port = port;
}

static void aux_display_disconnected(int port)
{
	/* Gets the other port. C0 -> C1, C1 -> C0. */
	int other_port = !port;

	/* If the current port is not the aux port, nothing needs to be done. */
	if (aux_port != port)
		return;

	/* If the other port is connected to a external display, switch aux. */
	if (dp_status[other_port] & DP_FLAGS_DP_ON)
		aux_switch_port(other_port);
	else
		aux_switch_port(AUX_PORT_NONE);
}

__override int svdm_dp_attention(int port, uint32_t *payload)
{
	int lvl = PD_VDO_DPSTS_HPD_LVL(payload[1]);
	int irq = PD_VDO_DPSTS_HPD_IRQ(payload[1]);
#ifdef CONFIG_USB_PD_DP_HPD_GPIO
	int cur_lvl = svdm_get_hpd_gpio(port);
#endif /* CONFIG_USB_PD_DP_HPD_GPIO */

	dp_status[port] = payload[1];

	if (chipset_in_state(CHIPSET_STATE_ANY_SUSPEND) &&
	    (irq || lvl))
		/*
		 * Wake up the AP.  IRQ or level high indicates a DP sink is now
		 * present.
		 */
		if (IS_ENABLED(CONFIG_MKBP_EVENT))
			pd_notify_dp_alt_mode_entry();

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
			usleep(svdm_hpd_deadline[port] - now);

		/* generate IRQ_HPD pulse */
		svdm_set_hpd_gpio(port, 0);
		usleep(HPD_DSTREAM_DEBOUNCE_IRQ);
		svdm_set_hpd_gpio(port, 1);
	} else {
		svdm_set_hpd_gpio(port, lvl);
	}

	/*
	 * Asurada can only output to 1 display port at a time.
	 * This implements FCFS policy by changing the aux channel. If a
	 * display is connected to the either port (says A), and the port A
	 * will be served until the display is disconnected from port A.
	 * It won't output to the other display which connects to port B.
	 */
	if (lvl && aux_port == AUX_PORT_NONE)
		/*
		 * A display is connected, and no display was plugged on either
		 * port.
		 */
		aux_switch_port(port);
	else if (!lvl)
		aux_display_disconnected(port);


	/* set the minimum time delay (2ms) for the next HPD IRQ */
	svdm_hpd_deadline[port] = get_time().val + HPD_USTREAM_DEBOUNCE_LVL;
#endif /* CONFIG_USB_PD_DP_HPD_GPIO */

	usb_mux_hpd_update(port, lvl, irq);

#ifdef USB_PD_PORT_TCPC_MST
	if (port == USB_PD_PORT_TCPC_MST)
		baseboard_mst_enable_control(port, lvl);
#endif

	/* ack */
	return 1;
}

__override void svdm_exit_dp_mode(int port)
{
	svdm_safe_dp_mode(port);
#ifdef CONFIG_USB_PD_DP_HPD_GPIO
	svdm_set_hpd_gpio(port, 0);
#endif /* CONFIG_USB_PD_DP_HPD_GPIO */
	usb_mux_hpd_update(port, 0, 0);

	aux_display_disconnected(port);

#ifdef USB_PD_PORT_TCPC_MST
	if (port == USB_PD_PORT_TCPC_MST)
		baseboard_mst_enable_control(port, 0);
#endif
#ifdef CONFIG_USB_PD_TCPMV2
	dp_teardown(port);
#endif
}

int pd_snk_is_vbus_provided(int port)
{
	return ppc_is_vbus_present(port);
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

#ifdef CONFIG_USB_PD_MAX_SINGLE_SOURCE_CURRENT
	/* Give back the current quota we are no longer using */
	charge_manager_source_port(port, 0);
#endif /* defined(CONFIG_USB_PD_MAX_SINGLE_SOURCE_CURRENT) */

	/* Notify host of power info change. */
	pd_send_host_event(PD_EVENT_POWER_CHANGE);
}

int pd_check_vconn_swap(int port)
{
	/* TODO: Only allow vconn swap if PP4200_G rail is enabled , s3/s0 on */
	return 0;
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

#ifdef CONFIG_USB_PD_MAX_SINGLE_SOURCE_CURRENT
	/* Ensure we advertise the proper available current quota */
	charge_manager_source_port(port, 1);
#endif /* defined(CONFIG_USB_PD_MAX_SINGLE_SOURCE_CURRENT) */

	/* Notify host of power info change. */
	pd_send_host_event(PD_EVENT_POWER_CHANGE);

	return EC_SUCCESS;
}

int board_vbus_source_enabled(int port)
{
	return ppc_is_sourcing_vbus(port);
}

