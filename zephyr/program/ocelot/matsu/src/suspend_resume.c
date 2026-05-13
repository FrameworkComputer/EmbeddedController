/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "ap_power/ap_power.h"
#include "gpio_signal.h"

#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(board_init, LOG_LEVEL_INF);

static void matsu_power_event_handler(struct ap_power_ev_callback *callback,
				      struct ap_power_ev_data data)
{
	switch (data.event) {
	case AP_POWER_PRE_INIT:
		/* fall-through */
	case AP_POWER_STARTUP:
		/* Deassert AMP_MUTE_ODL when AP is on. */
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_amp_mute_odl), 1);
		break;
	case AP_POWER_HARD_OFF:
		/* Assert AMP_MUTE_ODL when powered off. */
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_amp_mute_odl), 0);
		break;
	default:
		/* Other events ignored */
		break;
	}
}

static int init_suspend_resume(void)
{
	static struct ap_power_ev_callback cb;
	const struct gpio_dt_spec *amp_mute_odl =
		GPIO_DT_FROM_NODELABEL(gpio_amp_mute_odl);

	if (!gpio_is_ready_dt(amp_mute_odl)) {
		LOG_ERR_DEVICE_NOT_READY(amp_mute_odl->port);
		return -EINVAL;
	}

	ap_power_ev_init_callback(&cb, matsu_power_event_handler,
				  AP_POWER_PRE_INIT | AP_POWER_STARTUP |
					  AP_POWER_HARD_OFF);
	ap_power_ev_add_callback(&cb);

	return 0;
}
SYS_INIT(init_suspend_resume, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
