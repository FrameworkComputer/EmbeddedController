/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "atomic.h"
#include "charger.h"
#include "charge_manager.h"
#include "common.h"
#include "console.h"
#include "driver/charger/rt946x.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "registers.h"
#include "system.h"
#include "task.h"
#include "timer.h"
#include "util.h"
#include "usb_mux.h"
#include "usb_pd.h"
#include "usb_pd_tcpm.h"

#define CPRINTF(format, args...) cprintf(CC_USBPD, format, ## args)
#define CPRINTS(format, args...) cprints(CC_USBPD, format, ## args)

static uint8_t vbus_en;

int board_vbus_source_enabled(int port)
{
	return vbus_en;
}

int pd_check_vconn_swap(int port)
{
	/*
	 * VCONN is provided directly by the battery (PPVAR_SYS)
	 * but use the same rules as power swap.
	 */
	return pd_get_dual_role(port) == PD_DRP_TOGGLE_ON ? 1 : 0;
}

int pd_set_power_supply_ready(int port)
{
	/* Disable NCP3902 to avoid charging from VBUS */
	gpio_set_level(GPIO_NCP3902_EN_L, 1);

	/* Provide VBUS */
	vbus_en = 1;
	gpio_set_level(GPIO_EN_PP5000_USBC, 1);

	/* notify host of power info change */
	pd_send_host_event(PD_EVENT_POWER_CHANGE);

	return EC_SUCCESS; /* we are ready */
}

void pd_power_supply_reset(int port)
{
	int prev_en;

	prev_en = vbus_en;
	/* Disable VBUS */
	vbus_en = 0;

	if (prev_en) {
		gpio_set_level(GPIO_EN_PP5000_USBC, 0);
		msleep(250);
		gpio_set_level(GPIO_NCP3902_EN_L, 0);
	}

	/* notify host of power info change */
	pd_send_host_event(PD_EVENT_POWER_CHANGE);
}

/* ----------------- Vendor Defined Messages ------------------ */
#ifdef CONFIG_USB_PD_ALT_MODE_DFP
__override void svdm_dp_post_config(int port)
{
	dp_flags[port] |= DP_FLAGS_DP_ON;
	if (!(dp_flags[port] & DP_FLAGS_HPD_HI_PENDING))
		return;

	gpio_set_level(GPIO_USB_C0_HPD_OD, 1);
	gpio_set_level(GPIO_USB_C0_DP_OE_L, 0);
	gpio_set_level(GPIO_USB_C0_DP_POLARITY, pd_get_polarity(port));

	/* set the minimum time delay (2ms) for the next HPD IRQ */
	svdm_hpd_deadline[port] = get_time().val + HPD_USTREAM_DEBOUNCE_LVL;
	usb_mux_hpd_update(port, 1, 0);
}

__override int svdm_dp_attention(int port, uint32_t *payload)
{
	int cur_lvl = gpio_get_level(GPIO_USB_C0_HPD_OD);
	int lvl = PD_VDO_DPSTS_HPD_LVL(payload[1]);
	int irq = PD_VDO_DPSTS_HPD_IRQ(payload[1]);

	dp_status[port] = payload[1];

	/* Its initial DP status message prior to config */
	if (!(dp_flags[port] & DP_FLAGS_DP_ON)) {
		if (lvl)
			dp_flags[port] |= DP_FLAGS_HPD_HI_PENDING;
		return 1;
	}

	usb_mux_set(port, lvl ? USB_PD_MUX_DP_ENABLED : USB_PD_MUX_NONE,
		    USB_SWITCH_CONNECT, pd_get_polarity(port));

	usb_mux_hpd_update(port, lvl, irq);

	if (irq & cur_lvl) {
		uint64_t now = get_time().val;
		/* wait for the minimum spacing between IRQ_HPD if needed */
		if (now < svdm_hpd_deadline[port])
			usleep(svdm_hpd_deadline[port] - now);

		/* generate IRQ_HPD pulse */
		gpio_set_level(GPIO_USB_C0_HPD_OD, 0);
		usleep(HPD_DSTREAM_DEBOUNCE_IRQ);
		gpio_set_level(GPIO_USB_C0_HPD_OD, 1);

		gpio_set_level(GPIO_USB_C0_DP_OE_L, 0);
		gpio_set_level(GPIO_USB_C0_DP_POLARITY, pd_get_polarity(port));

		/* set the minimum time delay (2ms) for the next HPD IRQ */
		svdm_hpd_deadline[port] = get_time().val +
			HPD_USTREAM_DEBOUNCE_LVL;
	} else if (irq & !cur_lvl) {
		CPRINTF("ERR:HPD:IRQ&LOW\n");
		return 0; /* nak */
	} else {
		gpio_set_level(GPIO_USB_C0_HPD_OD, lvl);
		gpio_set_level(GPIO_USB_C0_DP_OE_L, !lvl);
		gpio_set_level(GPIO_USB_C0_DP_POLARITY, pd_get_polarity(port));
		/* set the minimum time delay (2ms) for the next HPD IRQ */
		svdm_hpd_deadline[port] = get_time().val +
			HPD_USTREAM_DEBOUNCE_LVL;
	}

	/* ack */
	return 1;
}

__override void svdm_exit_dp_mode(int port)
{
	svdm_safe_dp_mode(port);
	gpio_set_level(GPIO_USB_C0_HPD_OD, 0);
	gpio_set_level(GPIO_USB_C0_DP_OE_L, 1);
	usb_mux_hpd_update(port, 0, 0);
}
#endif /* CONFIG_USB_PD_ALT_MODE_DFP */
