/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "ap_power/ap_power.h"
#include "chipset.h"
#include "console.h"
#include "cros_board_info.h"
#include "cros_cbi.h"
#include "gpio/gpio_int.h"
#include "hooks.h"
#include "rauru_dp.h"
#include "timer.h"
#include "usb_pd.h"

#include <zephyr/logging/log.h>

#define CPRINTS(format, args...) cprints(CC_USBPD, format, ##args)
#define CPRINTF(format, args...) cprintf(CC_USBPD, format, ##args)

LOG_MODULE_DECLARE(rauru, CONFIG_RAURU_LOG_LEVEL);

static void hdmi_hpd_low_deferred(void)
{
	if (!rauru_is_hpd_high(DP_PORT_HDMI)) {
		rauru_detach_dp_path(DP_PORT_HDMI);
	}
}
DECLARE_DEFERRED(hdmi_hpd_low_deferred);

bool rauru_has_hdmi_port(void)
{
	static bool init;
	static bool rauru_has_hdmi;
	int ret;
	uint32_t val;

	if (init) {
		return rauru_has_hdmi;
	}

	init = true;
	ret = cros_cbi_get_fw_config(FW_HDMI, &val);
	if (ret != 0) {
		LOG_WRN("Error retrieving CBI FW_CONFIG field %d", FW_HDMI);
		return rauru_has_hdmi;
	}

	switch (val) {
	default:
		LOG_WRN("HDMI: Unknown %d", val);
		break;
	case FW_HDMI_NOT_PRESENT:
		rauru_has_hdmi = false;
		LOG_INF("HDMI: Not present");
		break;

	case FW_HDMI_PRESENT:
		rauru_has_hdmi = true;
		LOG_INF("HDMI: Present");
		break;
	}
	return rauru_has_hdmi;
}

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

	if (!rauru_has_hdmi_port()) {
		return;
	}
	switch (data.event) {
	default:
		return;

	case AP_POWER_RESUME:
		value = 1;
		gpio_enable_dt_interrupt(
			GPIO_INT_FROM_NODELABEL(int_hdmi_ec_hpd));
		break;

	case AP_POWER_SUSPEND:
		value = 0;
		gpio_disable_dt_interrupt(
			GPIO_INT_FROM_NODELABEL(int_hdmi_ec_hpd));
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
