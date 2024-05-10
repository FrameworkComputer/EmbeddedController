/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "ap_power/ap_power.h"
#include "chipset.h"
#include "console.h"
#include "hooks.h"
#include "rauru_dp.h"
#include "timer.h"
#include "usb_pd.h"

#define CPRINTS(format, args...) cprints(CC_USBPD, format, ##args)
#define CPRINTF(format, args...) cprintf(CC_USBPD, format, ##args)

static void hdmi_hpd_low_deferred(void)
{
	if (!rauru_is_hpd_high(DP_PORT_HDMI)) {
		rauru_detach_dp_path(DP_PORT_HDMI);
	}
}
DECLARE_DEFERRED(hdmi_hpd_low_deferred);

void hdmi_hpd_interrupt(enum gpio_signal signal)
{
	int hpd = gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_hdmi_ec_hpd));

	if (!rauru_is_dp_muxable(DP_PORT_HDMI)) {
		CPRINTS("p%d: The other port is already muxed.", DP_PORT_HDMI);
		return;
	}

	if (hpd) {
		rauru_set_dp_path(DP_PORT_HDMI);
		hook_call_deferred(&hdmi_hpd_low_deferred_data, -1);
	} else {
		hook_call_deferred(&hdmi_hpd_low_deferred_data,
				   HPD_USTREAM_DEBOUNCE_LVL);
	}

	svdm_set_hpd_gpio(DP_PORT_HDMI, hpd);
}

static void board_hdmi_suspend(struct ap_power_ev_callback *cb,
			       struct ap_power_ev_data data)
{
	int value;

	switch (data.event) {
	default:
		return;

	case AP_POWER_RESUME:
		value = 1;
		break;

	case AP_POWER_SUSPEND:
		value = 0;
		break;
	}
	gpio_pin_set_dt(GPIO_DT_FROM_ALIAS(gpio_en_hdmi_pwr), value);
}

static int board_hdmi_suspend_init(void)
{
	static struct ap_power_ev_callback cb = {
		.handler = board_hdmi_suspend,
		.events = AP_POWER_SUSPEND | AP_POWER_RESUME,
	};
	ap_power_ev_add_callback(&cb);
	return 0;
}
SYS_INIT(board_hdmi_suspend_init, APPLICATION, 0);
