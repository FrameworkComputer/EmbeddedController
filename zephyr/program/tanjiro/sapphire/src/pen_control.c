/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "chipset.h"
#include "cros_board_info.h"
#include "cros_cbi.h"
#include "gpio/gpio_int.h"
#include "gpio_signal.h"
#include "hooks.h"
#include "peripheral_charger.h"

#include <zephyr/drivers/gpio.h>
#include <zephyr/init.h>
#include <zephyr/logging/log.h>

#include <ap_power/ap_power.h>

LOG_MODULE_REGISTER(pen_control, LOG_LEVEL_ERR);

__override void board_pchg_power_on(int port, bool on)
{
	if (port != 0)
		return;

	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_pp5000_wlc_en), on);
}

static void stylus_int_init(void)
{
	uint32_t board_version;

	if (cbi_get_board_version(&board_version) != EC_SUCCESS) {
		LOG_ERR("Failed to get board version.");
		/* force enable stylus hall sensor detect function */
		board_version = 2;
	}

	if (board_version <= 1) {
		/* for EVT board, set interrupt always enable */
		gpio_pin_configure_dt(GPIO_DT_FROM_NODELABEL(gpio_pen_pres),
				      GPIO_OUTPUT);
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_pen_pres), false);
	}
}
DECLARE_HOOK(HOOK_INIT, stylus_int_init, HOOK_PRIO_PRE_DEFAULT);
