/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "ap_power/ap_power.h"
#include "gpio_signal.h"

#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(board_init, LOG_LEVEL_INF);

#define EN_PP3300_WLAN_DT_SPEC GPIO_DT_FROM_NODELABEL(gpio_ec_en_pp3300_wlan)

static void brox_power_event_handler(struct ap_power_ev_callback *callback,
				     struct ap_power_ev_data data)
{
	/*
	 * WLAN should be enabled during the transition from G3 to S5.
	 * However, the RPL always bounces temporarily back to S5
	 * on initial power up, so we need to also ensure WLAN is enabled
	 * during the transition from S5 to S3.
	 */
	switch (data.event) {
	case AP_POWER_PRE_INIT:
		/* fall-through */
	case AP_POWER_STARTUP:
		gpio_pin_set_dt(EN_PP3300_WLAN_DT_SPEC, 1);
		break;
	case AP_POWER_SHUTDOWN:
		gpio_pin_set_dt(EN_PP3300_WLAN_DT_SPEC, 0);
		break;
	default:
		/* Other events ignored */
		break;
	}
}

static int init_suspend_resume(void)
{
	static struct ap_power_ev_callback cb;
	const struct gpio_dt_spec *en_pp3300_wlan =
		GPIO_DT_FROM_NODELABEL(gpio_ec_en_pp3300_wlan);

	if (!gpio_is_ready_dt(en_pp3300_wlan)) {
		LOG_ERR("device %s not ready", en_pp3300_wlan->port->name);
		return -EINVAL;
	}

	ap_power_ev_init_callback(&cb, brox_power_event_handler,
				  AP_POWER_PRE_INIT | AP_POWER_STARTUP |
					  AP_POWER_SHUTDOWN);
	ap_power_ev_add_callback(&cb);

	return 0;
}
SYS_INIT(init_suspend_resume, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
