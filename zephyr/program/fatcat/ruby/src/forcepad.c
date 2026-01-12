/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "console.h"
#include "gpio/gpio_int.h"
#include "hooks.h"

#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

#include <cros_board_info.h>

LOG_MODULE_DECLARE(forcepad, CONFIG_LOG_DEFAULT_LEVEL);

#define INT_DELAY_US 500

static void forcepad_interrupt_change(void)
{
	int det = gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_en_fpad_seq));
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_fpad_5v_en), det);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_fpad_1p8v_en), det);
}
DECLARE_DEFERRED(forcepad_interrupt_change);

void forcepad_interrupt(enum gpio_signal s)
{
	hook_call_deferred(&forcepad_interrupt_change_data, INT_DELAY_US);
}

static void fpad_init(void)
{
	int ret;
	uint32_t board_version;

	ret = cbi_get_board_version(&board_version);
	if (ret != EC_SUCCESS) {
		LOG_ERR("Error retrieving CBI board version");
		return;
	}

	if (board_version < 2) {
		LOG_INF("Disable ForcePad Interrupt");
		gpio_disable_dt_interrupt(
			GPIO_INT_FROM_NODELABEL(int_en_fpad_seq));
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_fpad_5v_en), 1);
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_fpad_1p8v_en), 1);
	} else {
		LOG_INF("Enable ForcePad Interrupt");
		gpio_enable_dt_interrupt(
			GPIO_INT_FROM_NODELABEL(int_en_fpad_seq));
	}
}
DECLARE_HOOK(HOOK_INIT, fpad_init, HOOK_PRIO_DEFAULT);
