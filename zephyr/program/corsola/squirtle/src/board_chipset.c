/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Corsola baseboard-chipset specific configuration */

#include "charge_state.h"
#include "common.h"
#include "cros_board_info.h"
#include "gpio/gpio_int.h"
#include "gpio_signal.h"
#include "hooks.h"

#include <zephyr/drivers/gpio.h>
#include <zephyr/init.h>

#include <ap_power/ap_power.h>

LOG_MODULE_REGISTER(cbi_info);

#define BOARD_VERSION_UNKNOWN 0xffffffff
static bool value_en;

static void set_tp_en_pin(void)
{
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_tp_en), !value_en);
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
		hook_call_deferred(&set_bl_en_pin_data, 0);
		hook_call_deferred(&set_tp_en_pin_data, 30 * MSEC);
	} else {
		value_en = false;
		hook_call_deferred(&set_tp_en_pin_data, 0);
		hook_call_deferred(&set_bl_en_pin_data, 102 * MSEC);
	}
}

static uint32_t board_version = BOARD_VERSION_UNKNOWN;
static void ap_bl_en_init(void)
{
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_ap_bl_en));
}
DECLARE_HOOK(HOOK_INIT, ap_bl_en_init, HOOK_PRIO_DEFAULT);

static void board_backlight_handler(struct ap_power_ev_callback *cb,
				    struct ap_power_ev_data data)
{
	int value;

	switch (data.event) {
	default:
		return;

	case AP_POWER_RESUME:
		/* Called on AP S3 -> S0 transition */
		value = 1;
		break;

	case AP_POWER_SUSPEND:
		/* Called on AP S0 -> S3 transition */
		value = 0;
		break;
	}
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_bl_en_od), value);
}

static int install_backlight_handler(void)
{
	/*
	 *Check board version to enable older power sequence.
	 *Only when the board ID is equal to 0, the old sequence needs to be
	 *enabled.
	 */
	if (board_version == BOARD_VERSION_UNKNOWN || IS_ENABLED(CONFIG_TEST)) {
		if (cbi_get_board_version(&board_version) != EC_SUCCESS) {
			LOG_ERR("Failed to get board version.");
			board_version = 0;
		}
	}

	if (board_version == 0) {
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
