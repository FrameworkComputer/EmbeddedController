/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "ap_power/ap_power.h"
#include "gpio_signal.h"

#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(board_init, LOG_LEVEL_ERR);

#define EN_PP3300_WLAN_DT_SPEC GPIO_DT_FROM_NODELABEL(gpio_ec_en_pp3300_wlan)

static void brox_suspend_resume_handler(struct ap_power_ev_callback *callback,
					struct ap_power_ev_data data)
{
	switch (data.event) {
	case AP_POWER_RESUME:
		gpio_pin_set_dt(EN_PP3300_WLAN_DT_SPEC, 1);
		break;
	case AP_POWER_SUSPEND:
		gpio_pin_set_dt(EN_PP3300_WLAN_DT_SPEC, 0);
		break;
	default:
		/* Other events ignored */
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

	ap_power_ev_init_callback(&cb, brox_suspend_resume_handler,
				  AP_POWER_RESUME | AP_POWER_SUSPEND);
	ap_power_ev_add_callback(&cb);

	return 0;
}
SYS_INIT(init_suspend_resume, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
