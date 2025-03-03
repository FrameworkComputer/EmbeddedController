/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "cros_board_info.h"
#include "gpio/gpio_int.h"
#include "gpio_signal.h"
#include "hooks.h"
#include "timer.h"

#include <zephyr/drivers/gpio.h>
#include <zephyr/init.h>

#include <ap_power/ap_power.h>

LOG_MODULE_REGISTER(cbi_info);
static bool value_en;

static void set_tp_en_pin(void)
{
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_tp_en_od), value_en);
}
DECLARE_DEFERRED(set_tp_en_pin);

static void set_bl_en_pin(void)
{
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_bl_en_od), value_en);
}
DECLARE_DEFERRED(set_bl_en_pin);

void ap_bl_en_interrupt(enum gpio_signal signal)
{
	if (gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_ap_bl_en_odl))) {
		value_en = true;
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_bl_en_od),
				value_en);
		hook_call_deferred(&set_tp_en_pin_data, 3 * USEC_PER_MSEC);
	} else {
		value_en = false;
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_tp_en_od),
				value_en);
		hook_call_deferred(&set_bl_en_pin_data, 3 * USEC_PER_MSEC);
	}
}

static void ap_bl_en_init(void)
{
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_ap_bl_en));
}
DECLARE_HOOK(HOOK_INIT, ap_bl_en_init, HOOK_PRIO_DEFAULT);

static void board_backlight_handler(struct ap_power_ev_callback *cb,
				    struct ap_power_ev_data data)
{
	switch (data.event) {
	default:
		return;

	case AP_POWER_RESUME:
	case AP_POWER_SUSPEND:
		int value = data.event == AP_POWER_RESUME ? 1 : 0;
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_bl_en_od),
				value);
		break;
	}
}

static int install_backlight_handler(void)
{
	/*
	 * Check board version to enable older power sequence.
	 * Only when the board ID is not greater than 1, the old sequence needs
	 * to be enabled.
	 */
	uint32_t board_version;

	if (cbi_get_board_version(&board_version) != EC_SUCCESS) {
		LOG_ERR("Failed to get board version.");
		board_version = 0;
	}

	if (board_version <= 1) {
		static struct ap_power_ev_callback cb;

		/*
		 * Add a callback for suspend/resume to
		 * control the keyboard backlight.
		 */
		ap_power_ev_init_callback(&cb, board_backlight_handler,
					  AP_POWER_RESUME | AP_POWER_SUSPEND);
		ap_power_ev_add_callback(&cb);
	}
	return 0;
}

SYS_INIT(install_backlight_handler, APPLICATION, 1);
