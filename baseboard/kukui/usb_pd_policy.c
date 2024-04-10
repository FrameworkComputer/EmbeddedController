/* Copyright 2018 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "charge_manager.h"
#include "charge_state.h"
#include "charger.h"
#include "console.h"
#include "gpio.h"
#include "hooks.h"
#include "system.h"
#include "timer.h"
#include "usb_mux.h"
#include "usb_pd.h"
#include "usb_pd_policy.h"
#include "util.h"

#define CPRINTF(format, args...) cprintf(CC_USBPD, format, ##args)
#define CPRINTS(format, args...) cprints(CC_USBPD, format, ##args)

static int board_get_polarity(int port)
{
	/* Krane's aux mux polarity is reversed. Workaround to flip it back. */
	if (IS_ENABLED(BOARD_KRANE) && board_get_version() == 3)
		return !polarity_rm_dts(pd_get_polarity(port));

	return polarity_rm_dts(pd_get_polarity(port));
}

static uint8_t vbus_en;

#define VBUS_EN_SYSJUMP_TAG 0x5645 /* VE */
#define VBUS_EN_HOOK_VERSION 1

static void vbus_en_preserve_state(void)
{
	system_add_jump_tag(VBUS_EN_SYSJUMP_TAG, VBUS_EN_HOOK_VERSION,
			    sizeof(vbus_en), &vbus_en);
}
DECLARE_HOOK(HOOK_SYSJUMP, vbus_en_preserve_state, HOOK_PRIO_DEFAULT);

static void vbus_en_restore_state(void)
{
	const uint8_t *prev_vbus_en;
	int size, version;

	prev_vbus_en = (const uint8_t *)system_get_jump_tag(VBUS_EN_SYSJUMP_TAG,
							    &version, &size);

	if (prev_vbus_en && version == VBUS_EN_HOOK_VERSION &&
	    size == sizeof(*prev_vbus_en)) {
		memcpy(&vbus_en, prev_vbus_en, sizeof(vbus_en));
	}
}
DECLARE_HOOK(HOOK_INIT, vbus_en_restore_state, HOOK_PRIO_DEFAULT);

int board_vbus_source_enabled(int port)
{
	return vbus_en;
}

int board_is_sourcing_vbus(int port)
{
	if (IS_ENABLED(BOARD_KUKUI) && board_get_version() <= 1)
		return charger_is_sourcing_otg_power(port);
	else
		return board_vbus_source_enabled(port);
}

int pd_set_power_supply_ready(int port)
{
	if (port != CHARGE_PORT_USB_C)
		return EC_ERROR_INVAL;

	pd_set_vbus_discharge(port, 0);
	/* Provide VBUS */
	vbus_en = 1;

#ifdef CONFIG_USB_PD_MAX_SINGLE_SOURCE_CURRENT
	/* Ensure we advertise the proper available current quota */
	charge_manager_source_port(port, 1);
#endif /* defined(CONFIG_USB_PD_MAX_SINGLE_SOURCE_CURRENT) */

	if (IS_ENABLED(VARIANT_KUKUI_CHARGER_ISL9238))
		charge_set_output_current_limit(CHARGER_SOLO, 3300, 5000);
	else
		charger_enable_otg_power(CHARGER_SOLO, 1);

	gpio_set_level(GPIO_EN_USBC_CHARGE_L, 1);
	gpio_set_level(GPIO_EN_PP5000_USBC, 1);
	if (IS_ENABLED(CONFIG_CHARGER_OTG) &&
	    IS_ENABLED(CONFIG_CHARGER_ISL9238C))
		charger_set_current(CHARGER_SOLO, 0);

	/* notify host of power info change */
	pd_send_host_event(PD_EVENT_POWER_CHANGE);

	return EC_SUCCESS; /* we are ready */
}

void pd_power_supply_reset(int port)
{
	int prev_en;

	if (port != CHARGE_PORT_USB_C)
		return;

	prev_en = vbus_en;
	/* Disable VBUS */
	vbus_en = 0;
	/* Enable discharge if we were previously sourcing 5V */
	if (prev_en)
		pd_set_vbus_discharge(port, 1);

#ifdef CONFIG_USB_PD_MAX_SINGLE_SOURCE_CURRENT
	/* Give back the current quota we are no longer using */
	charge_manager_source_port(port, 0);
#endif /* defined(CONFIG_USB_PD_MAX_SINGLE_SOURCE_CURRENT) */

	if (IS_ENABLED(VARIANT_KUKUI_CHARGER_ISL9238))
		charge_set_output_current_limit(CHARGER_SOLO, 0, 0);
	else
		charger_enable_otg_power(CHARGER_SOLO, 0);

	gpio_set_level(GPIO_EN_PP5000_USBC, 0);

	/* notify host of power info change */
	pd_send_host_event(PD_EVENT_POWER_CHANGE);
}

int pd_check_vconn_swap(int port)
{
	/* always allow vconn swap, since PSYS sources VCONN */
	return 1;
}

/* ----------------- Vendor Defined Messages ------------------ */
#ifdef CONFIG_USB_PD_ALT_MODE_DFP
__overridable int board_has_virtual_mux(void)
{
	return IS_ENABLED(CONFIG_USB_MUX_VIRTUAL);
}

static void board_usb_mux_set(int port, mux_state_t mux_mode,
			      enum usb_switch usb_mode, int polarity)
{
	usb_mux_set(port, mux_mode, usb_mode, polarity);

	if (!board_has_virtual_mux())
		/* b:149181702: Inform AP of DP status */
		host_set_single_event(EC_HOST_EVENT_USB_MUX);
}

__override void svdm_safe_dp_mode(int port)
{
	/* make DP interface safe until configure */
	dp_flags[port] = 0;
	dp_status[port] = 0;
	board_usb_mux_set(port, USB_PD_MUX_NONE, USB_SWITCH_CONNECT,
			  board_get_polarity(port));
}

__override int svdm_enter_dp_mode(int port, uint32_t mode_caps)
{
	/* Kukui/Krane doesn't support superspeed lanes. */
	const uint32_t support_pin_mode =
		board_has_virtual_mux() ? (MODE_DP_PIN_C | MODE_DP_PIN_E) :
					  MODE_DP_PIN_ALL;

	/**
	 * Only enter mode if device is DFP_D (and PIN_C/E for Kukui/Krane)
	 * capable
	 */
	if ((mode_caps & MODE_DP_SNK) &&
	    (mode_caps & ((support_pin_mode << MODE_DP_DFP_PIN_SHIFT) |
			  (support_pin_mode << MODE_DP_UFP_PIN_SHIFT)))) {
		svdm_safe_dp_mode(port);
		return 0;
	}

	CPRINTS("ERR:DP mode SNK or C&E missing! 0x%x", mode_caps);
	return -1;
}

__override int svdm_dp_config(int port, uint32_t *payload)
{
	int opos = pd_alt_mode(port, TCPCI_MSG_SOP, USB_SID_DISPLAYPORT);
	int status = dp_status[port];
	int mf_pref = PD_VDO_DPSTS_MF_PREF(dp_status[port]);
	int pin_mode;

	/* Kukui doesn't support multi-function mode, mask it out. */
	if (board_has_virtual_mux())
		status &= ~PD_VDO_DPSTS_MF_MASK;

	pin_mode = pd_dfp_dp_get_pin_mode(port, status);

	if (!pin_mode)
		return 0;

	if (board_has_virtual_mux())
		board_usb_mux_set(port, USB_PD_MUX_DP_ENABLED,
				  USB_SWITCH_CONNECT, board_get_polarity(port));
	else
		board_usb_mux_set(
			port, mf_pref ? USB_PD_MUX_DOCK : USB_PD_MUX_DP_ENABLED,
			USB_SWITCH_CONNECT, board_get_polarity(port));

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
	if (!(dp_flags[port] & DP_FLAGS_HPD_HI_PENDING))
		return;

	gpio_set_level(GPIO_USB_C0_HPD_OD, 1);
#ifdef VARIANT_KUKUI_DP_MUX_GPIO
	board_set_dp_mux_control(1, board_get_polarity(port));
#endif

	/* set the minimum time delay (2ms) for the next HPD IRQ */
	svdm_hpd_deadline[port] = get_time().val + HPD_USTREAM_DEBOUNCE_LVL;

	usb_mux_hpd_update(port,
			   USB_PD_MUX_HPD_LVL | USB_PD_MUX_HPD_IRQ_DEASSERTED);
}

__override int svdm_dp_attention(int port, uint32_t *payload)
{
	int cur_lvl = gpio_get_level(GPIO_USB_C0_HPD_OD);
	int lvl = PD_VDO_DPSTS_HPD_LVL(payload[1]);
	int irq = PD_VDO_DPSTS_HPD_IRQ(payload[1]);
	mux_state_t mux_state;

	dp_status[port] = payload[1];

	/* Its initial DP status message prior to config */
	if (!(dp_flags[port] & DP_FLAGS_DP_ON)) {
		if (lvl)
			dp_flags[port] |= DP_FLAGS_HPD_HI_PENDING;
		return 1;
	}

	mux_state = (lvl ? USB_PD_MUX_HPD_LVL : USB_PD_MUX_HPD_LVL_DEASSERTED) |
		    (irq ? USB_PD_MUX_HPD_IRQ : USB_PD_MUX_HPD_IRQ_DEASSERTED);
	usb_mux_hpd_update(port, mux_state);

	if (irq & cur_lvl) {
		uint64_t now = get_time().val;
		/* wait for the minimum spacing between IRQ_HPD if needed */
		if (now < svdm_hpd_deadline[port])
			crec_usleep(svdm_hpd_deadline[port] - now);

		/* generate IRQ_HPD pulse */
		gpio_set_level(GPIO_USB_C0_HPD_OD, 0);
		crec_usleep(HPD_DSTREAM_DEBOUNCE_IRQ);
		gpio_set_level(GPIO_USB_C0_HPD_OD, 1);

#ifdef VARIANT_KUKUI_DP_MUX_GPIO
		board_set_dp_mux_control(1, board_get_polarity(port));
#endif

		/* set the minimum time delay (2ms) for the next HPD IRQ */
		svdm_hpd_deadline[port] =
			get_time().val + HPD_USTREAM_DEBOUNCE_LVL;
	} else if (irq & !lvl) {
		CPRINTF("ERR:HPD:IRQ&LOW\n");
		return 0; /* nak */
	} else {
		gpio_set_level(GPIO_USB_C0_HPD_OD, lvl);
#ifdef VARIANT_KUKUI_DP_MUX_GPIO
		board_set_dp_mux_control(lvl, board_get_polarity(port));
#endif
		/* set the minimum time delay (2ms) for the next HPD IRQ */
		svdm_hpd_deadline[port] =
			get_time().val + HPD_USTREAM_DEBOUNCE_LVL;
	}

	/* ack */
	return 1;
}

__override void svdm_exit_dp_mode(int port)
{
	gpio_set_level(GPIO_USB_C0_HPD_OD, 0);
#ifdef VARIANT_KUKUI_DP_MUX_GPIO
	board_set_dp_mux_control(0, 0);
#endif
	usb_mux_hpd_update(port, USB_PD_MUX_HPD_LVL_DEASSERTED |
					 USB_PD_MUX_HPD_IRQ_DEASSERTED);
}
#endif /* CONFIG_USB_PD_ALT_MODE_DFP */
