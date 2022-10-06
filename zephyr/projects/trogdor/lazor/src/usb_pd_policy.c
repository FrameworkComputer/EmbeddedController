/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/drivers/gpio.h>

#include "charge_manager.h"
#include "chipset.h"
#include "console.h"
#include "system.h"
#include "usb_mux.h"
#include "usbc_ppc.h"
#include "util.h"

#define CPRINTS(format, args...) cprints(CC_USBCHARGE, format, ##args)
#define CPRINTF(format, args...) cprintf(CC_USBCHARGE, format, ##args)

int pd_check_vconn_swap(int port)
{
	/* In G3, do not allow vconn swap since PP5000 rail is off */
	return gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_en_pp5000_a));
}

static uint8_t vbus_en[CONFIG_USB_PD_PORT_MAX_COUNT];
#if CONFIG_USB_PD_PORT_MAX_COUNT == 1
static uint8_t vbus_rp[CONFIG_USB_PD_PORT_MAX_COUNT] = { TYPEC_RP_1A5 };
#else
static uint8_t vbus_rp[CONFIG_USB_PD_PORT_MAX_COUNT] = { TYPEC_RP_1A5,
							 TYPEC_RP_1A5 };
#endif

static void board_vbus_update_source_current(int port)
{
	/* Both port are controlled by PPC SN5S330. */
	ppc_set_vbus_source_current_limit(port, vbus_rp[port]);
	ppc_vbus_source_enable(port, vbus_en[port]);
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
	return tcpm_check_vbus_level(port, VBUS_PRESENT);
}

/* ----------------- Vendor Defined Messages ------------------ */
#ifdef CONFIG_USB_PD_ALT_MODE_DFP
__override int svdm_dp_config(int port, uint32_t *payload)
{
	int opos = pd_alt_mode(port, TCPCI_MSG_SOP, USB_SID_DISPLAYPORT);
	uint8_t pin_mode = get_dp_pin_mode(port);

	if (!pin_mode)
		return 0;

	/*
	 * Defer setting the usb_mux until HPD goes high, svdm_dp_attention().
	 * The AP only supports one DP phy. An external DP mux switches between
	 * the two ports. Should switch those muxes when it is really used,
	 * i.e. HPD high; otherwise, the real use case is preempted, like:
	 *  (1) plug a dongle without monitor connected to port-0,
	 *  (2) plug a dongle without monitor connected to port-1,
	 *  (3) plug a monitor to the port-1 dongle.
	 */

	payload[0] =
		VDO(USB_SID_DISPLAYPORT, 1, CMD_DP_CONFIG | VDO_OPOS(opos));
	payload[1] = VDO_DP_CFG(pin_mode, /* pin mode */
				1, /* DPv1.3 signaling */
				2); /* UFP connected */
	return 2;
};

__override void svdm_dp_post_config(int port)
{
	dp_flags[port] |= DP_FLAGS_DP_ON;
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

	for (i = 0; i < CONFIG_USB_PD_PORT_MAX_COUNT; i++)
		if (i != port) {
			if (usb_mux_get(i) & USB_PD_MUX_DP_ENABLED)
				return 0;
		}

	return 1;
}

__override int svdm_dp_attention(int port, uint32_t *payload)
{
	const struct gpio_dt_spec *hpd =
		GPIO_DT_FROM_NODELABEL(gpio_dp_hot_plug_det);
	int lvl = PD_VDO_DPSTS_HPD_LVL(payload[1]);
	int irq = PD_VDO_DPSTS_HPD_IRQ(payload[1]);
	int cur_lvl = gpio_pin_get_dt(hpd);
	mux_state_t mux_state;

	dp_status[port] = payload[1];

	if (!is_dp_muxable(port)) {
		/* TODO(waihong): Info user? */
		CPRINTS("p%d: The other port is already muxed.", port);
		return 0;
	}

	/*
	 * Initial implementation to handle HPD. Only the first-plugged port
	 * works, i.e. sending HPD signal to AP. The second-plugged port
	 * will be ignored.
	 *
	 * TODO(waihong): Continue the above case, if the first-plugged port
	 * is then unplugged, switch to the second-plugged port and signal AP?
	 */
	if (lvl) {
		/*
		 * Enable and switch the DP port selection mux to the
		 * correct port.
		 *
		 * TODO(waihong): Better to move switching DP mux to
		 * the usb_mux abstraction.
		 */
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_dp_mux_sel),
				port == 1);
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_dp_mux_oe_l), 0);

		/* Connect the SBU lines in PPC chip. */
		if (IS_ENABLED(CONFIG_USBC_PPC_SBU))
			ppc_set_sbu(port, 1);

		/*
		 * Connect the USB SS/DP lines in TCPC chip.
		 *
		 * When mf_pref not true, still use the dock muxing
		 * because of the board USB-C topology (limited to 2
		 * lanes DP).
		 */
		usb_mux_set(port, USB_PD_MUX_DOCK, USB_SWITCH_CONNECT,
			    polarity_rm_dts(pd_get_polarity(port)));
	} else {
		/* Disconnect the DP port selection mux. */
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_dp_mux_oe_l), 1);
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_dp_mux_sel), 0);

		/* Disconnect the SBU lines in PPC chip. */
		if (IS_ENABLED(CONFIG_USBC_PPC_SBU))
			ppc_set_sbu(port, 0);

		/* Disconnect the DP but keep the USB SS lines in TCPC chip. */
		usb_mux_set(port, USB_PD_MUX_USB_ENABLED, USB_SWITCH_CONNECT,
			    polarity_rm_dts(pd_get_polarity(port)));
	}

	if (chipset_in_state(CHIPSET_STATE_ANY_SUSPEND) && (irq || lvl))
		/*
		 * Wake up the AP.  IRQ or level high indicates a DP sink is now
		 * present.
		 */
		pd_notify_dp_alt_mode_entry(port);

	/* Configure TCPC for the HPD event, for proper muxing */
	mux_state = (lvl ? USB_PD_MUX_HPD_LVL : USB_PD_MUX_HPD_LVL_DEASSERTED) |
		    (irq ? USB_PD_MUX_HPD_IRQ : USB_PD_MUX_HPD_IRQ_DEASSERTED);
	usb_mux_hpd_update(port, mux_state);

	/* Signal AP for the HPD event, through GPIO to AP */
	if (irq & cur_lvl) {
		uint64_t now = get_time().val;
		/* Wait for the minimum spacing between IRQ_HPD if needed */
		if (now < svdm_hpd_deadline[port])
			usleep(svdm_hpd_deadline[port] - now);

		/* Generate IRQ_HPD pulse */
		gpio_pin_set_dt(hpd, 0);
		usleep(HPD_DSTREAM_DEBOUNCE_IRQ);
		gpio_pin_set_dt(hpd, 1);

		/* Set the minimum time delay (2ms) for the next HPD IRQ */
		svdm_hpd_deadline[port] =
			get_time().val + HPD_USTREAM_DEBOUNCE_LVL;
	} else if (irq & !lvl) {
		CPRINTF("ERR:HPD:IRQ&LOW\n");
		return 0;
	}
	gpio_pin_set_dt(hpd, lvl);
	/* Set the minimum time delay (2ms) for the next HPD IRQ */
	svdm_hpd_deadline[port] = get_time().val + HPD_USTREAM_DEBOUNCE_LVL;

	return 1;
}

__override void svdm_exit_dp_mode(int port)
{
	if (is_dp_muxable(port)) {
		/* Disconnect the DP port selection mux. */
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_dp_mux_oe_l), 1);
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_dp_mux_sel), 0);

		/* Signal AP for the HPD low event */
		usb_mux_hpd_update(port, USB_PD_MUX_HPD_LVL_DEASSERTED |
						 USB_PD_MUX_HPD_IRQ_DEASSERTED);
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_dp_hot_plug_det),
				0);
	}
}
#endif /* CONFIG_USB_PD_ALT_MODE_DFP */
