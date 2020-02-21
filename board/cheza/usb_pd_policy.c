/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "charge_manager.h"
#include "console.h"
#include "gpio.h"
#include "pi3usb9281.h"
#include "system.h"
#include "usb_mux.h"
#include "usbc_ppc.h"
#include "util.h"

#define CPRINTS(format, args...) cprints(CC_USBCHARGE, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_USBCHARGE, format, ## args)

int pd_check_vconn_swap(int port)
{
	/* TODO(waihong): Check any case we do not allow. */
	return 1;
}

__override void pd_execute_data_swap(int port,
				     enum pd_data_role data_role)
{
	int enable = (data_role == PD_ROLE_UFP);
	int type;

	/*
	 * Exclude the PD charger, in which the "USB Communications Capable"
	 * bit is unset in the Fixed Supply PDO.
	 */
	if (pd_capable(port))
		enable = enable && pd_get_partner_usb_comm_capable(port);

	/*
	 * The hub behind the BC1.2 chip may advertise a BC1.2 type. So
	 * disconnect the switch when getting the charger type to ensure
	 * the detected type is from external.
	 */
	usb_charger_set_switches(port, USB_SWITCH_DISCONNECT);
	type = pi3usb9281_get_device_type(port);
	usb_charger_set_switches(port, USB_SWITCH_RESTORE);

	/* Exclude the BC1.2 charger, which is not detected as CDP or SDP. */
	enable = enable && (type & (PI3USB9281_TYPE_CDP | PI3USB9281_TYPE_SDP));

	/* Only mux one port to AP. If already muxed, return. */
	if (enable && (!gpio_get_level(GPIO_USB_C0_HS_MUX_SEL) ||
		       gpio_get_level(GPIO_USB_C1_HS_MUX_SEL)))
		return;

	/* Port-0 and port-1 have different polarities. */
	if (port == 0)
		gpio_set_level(GPIO_USB_C0_HS_MUX_SEL, enable ? 0 : 1);
	else if (port == 1)
		gpio_set_level(GPIO_USB_C1_HS_MUX_SEL, enable ? 1 : 0);
}

static uint8_t vbus_en[CONFIG_USB_PD_PORT_MAX_COUNT];
static uint8_t vbus_rp[CONFIG_USB_PD_PORT_MAX_COUNT] = {TYPEC_RP_1A5,
							TYPEC_RP_1A5};

static void board_vbus_update_source_current(int port)
{
	if (port == 0) {
		/*
		 * Port 0 is controlled by a USB-C PPC SN5S330.
		 */
		ppc_set_vbus_source_current_limit(port, vbus_rp[port]);
		ppc_vbus_source_enable(port, vbus_en[port]);
	} else if (port == 1) {
		/*
		 * Port 1 is controlled by a USB-C current-limited power
		 * switch, NX5P3290.   Change the GPIO driving the load switch.
		 *
		 * 1.5 vs 3.0 A limit is controlled by a dedicated gpio.
		 * If the GPIO is asserted, it shorts a n-MOSFET to put a
		 * 16.5k resistance (2x 33k in parallel) on the NX5P3290 load
		 * switch ILIM pin, setting a minimum OCP current of 3100 mA.
		 * If the GPIO is deasserted, the n-MOSFET is open that makes
		 * a single 33k resistor on ILIM, setting a minimum OCP
		 * current of 1505 mA.
		 */
		gpio_set_level(GPIO_EN_USB_C1_3A,
			       vbus_rp[port] == TYPEC_RP_3A0 ? 1 : 0);
		gpio_set_level(GPIO_EN_USB_C1_5V_OUT, vbus_en[port]);
	}
}

void pd_power_supply_reset(int port)
{
	int prev_en;

	prev_en = vbus_en[port];

	/* Disable VBUS */
	vbus_en[port] = 0;
	board_vbus_update_source_current(port);

	/* Enable discharge if we were previously sourcing 5V */
	if (prev_en)
		pd_set_vbus_discharge(port, 1);

#ifdef CONFIG_USB_PD_MAX_SINGLE_SOURCE_CURRENT
	/* Give back the current quota we are no longer using */
	charge_manager_source_port(port, 0);
#endif /* defined(CONFIG_USB_PD_MAX_SINGLE_SOURCE_CURRENT) */

	/* notify host of power info change */
	pd_send_host_event(PD_EVENT_POWER_CHANGE);
}

int pd_set_power_supply_ready(int port)
{
	/* Disable charging */
	board_vbus_sink_enable(port, 0);

	pd_set_vbus_discharge(port, 0);

	/* Provide VBUS */
	vbus_en[port] = 1;
	board_vbus_update_source_current(port);

	/* Ensure we advertise the proper available current quota */
	charge_manager_source_port(port, 1);

	/* notify host of power info change */
	pd_send_host_event(PD_EVENT_POWER_CHANGE);

	return EC_SUCCESS; /* we are ready */
}

int board_vbus_source_enabled(int port)
{
	return vbus_en[port];
}

__override void typec_set_source_current_limit(int port, enum tcpc_rp_value rp)
{
	vbus_rp[port] = rp;
	board_vbus_update_source_current(port);
}

int pd_snk_is_vbus_provided(int port)
{
	return !gpio_get_level(port ? GPIO_USB_C1_VBUS_DET_L :
				      GPIO_USB_C0_VBUS_DET_L);
}

/* ----------------- Vendor Defined Messages ------------------ */
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

	for (i = 0; i < CONFIG_USB_PD_PORT_MAX_COUNT; i++)
		if (i != port) {
			if (usb_mux_get(i) & USB_PD_MUX_DP_ENABLED)
				return 0;
		}

	return 1;
}

extern uint32_t dp_status[CONFIG_USB_PD_PORT_MAX_COUNT];
__override int svdm_dp_attention(int port, uint32_t *payload)
{
	int lvl = PD_VDO_DPSTS_HPD_LVL(payload[1]);
	int irq = PD_VDO_DPSTS_HPD_IRQ(payload[1]);
	int mf_pref = PD_VDO_DPSTS_MF_PREF(payload[1]);

	dp_status[port] = payload[1];

	usb_mux_hpd_update(port, lvl, irq);

	if (lvl && is_dp_muxable(port)) {
		/*
		 * The GPIO USBC_MUX_CONF1 enables the mux of the DP redriver
		 * for the port 1.
		 */
		gpio_set_level(GPIO_USBC_MUX_CONF1, port == 1);

		usb_mux_set(port, mf_pref ?
			    USB_PD_MUX_DOCK : USB_PD_MUX_DP_ENABLED,
			    USB_SWITCH_CONNECT, pd_get_polarity(port));
	} else {
		usb_mux_set(port, mf_pref ?
			    USB_PD_MUX_USB_ENABLED : USB_PD_MUX_NONE,
			    USB_SWITCH_CONNECT, pd_get_polarity(port));
	}

	/* ack */
	return 1;
}
