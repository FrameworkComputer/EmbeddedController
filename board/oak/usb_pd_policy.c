/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "atomic.h"
#include "charge_manager.h"
#include "common.h"
#include "console.h"
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

#define CPRINTF(format, args...) cprintf(CC_USBPD, format, ## args)
#define CPRINTS(format, args...) cprints(CC_USBPD, format, ## args)

int pd_set_power_supply_ready(int port)
{
	/* Disable charging */
	gpio_set_level(port ? GPIO_USB_C1_CHARGE_L :
			      GPIO_USB_C0_CHARGE_L, 1);
	/* Provide VBUS */
	gpio_set_level(port ? GPIO_USB_C1_5V_EN :
			      GPIO_USB_C0_5V_EN, 1);

	/* notify host of power info change */
	pd_send_host_event(PD_EVENT_POWER_CHANGE);

	return EC_SUCCESS; /* we are ready */
}

void pd_power_supply_reset(int port)
{
	/* Disable VBUS */
	gpio_set_level(port ? GPIO_USB_C1_5V_EN :
			      GPIO_USB_C0_5V_EN, 0);

	/* notify host of power info change */
	pd_send_host_event(PD_EVENT_POWER_CHANGE);
}

__override int pd_board_checks(void)
{
#if BOARD_REV <= OAK_REV3
	/* wake up VBUS task to check vbus change */
	task_wake(TASK_ID_VBUS);
#endif
	return EC_SUCCESS;
}

int pd_check_vconn_swap(int port)
{
	/* in G3, do not allow vconn swap since 5V power source is off */
	return gpio_get_level(GPIO_5V_POWER_GOOD);
}

/* ----------------- Vendor Defined Messages ------------------ */
#ifdef CONFIG_USB_PD_ALT_MODE_DFP
__override void svdm_dp_post_config(int port)
{
	dp_flags[port] |= DP_FLAGS_DP_ON;
	if (!(dp_flags[port] & DP_FLAGS_HPD_HI_PENDING))
		return;
	board_typec_dp_set(port, 1);
}

__override int svdm_dp_attention(int port, uint32_t *payload)
{
	int cur_lvl;
	int lvl = PD_VDO_DPSTS_HPD_LVL(payload[1]);
	int irq = PD_VDO_DPSTS_HPD_IRQ(payload[1]);

	dp_status[port] = payload[1];
	cur_lvl = gpio_get_level(GPIO_USB_DP_HPD);

	/* Its initial DP status message prior to config */
	if (!(dp_flags[port] & DP_FLAGS_DP_ON)) {
		if (lvl)
			dp_flags[port] |= DP_FLAGS_HPD_HI_PENDING;
		return 1;
	}

	if (irq & cur_lvl) {
		board_typec_dp_on(port);
	} else if (irq & !cur_lvl) {
		CPRINTF("ERR:HPD:IRQ&LOW\n");
		return 0; /* nak */
	} else {
		board_typec_dp_set(port, lvl);
	}
	/* ack */
	return 1;
}

__override void svdm_exit_dp_mode(int port)
{
	svdm_safe_dp_mode(port);
	board_typec_dp_off(port, dp_flags);
}
#endif /* CONFIG_USB_PD_ALT_MODE_DFP */
