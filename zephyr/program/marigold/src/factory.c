/*
 * Copyright 2023 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/drivers/gpio.h>

#include "ec_commands.h"
#include "factory.h"
#include "gpio.h"
#include "gpio/gpio_int.h"
#include "hooks.h"
#include "keyboard_protocol.h"

static uint8_t factory_enable;

static void fake_power_button(void);
DECLARE_DEFERRED(fake_power_button);

void factory_setting(uint8_t enable)
{
	factory_enable = enable;

	/* Ignored the power button signal if we are in the factory mode */
	if (factory_enable) {
		hook_call_deferred(&fake_power_button_data, 250 * MSEC);
		gpio_disable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_powerbtn));
	} else {
		hook_call_deferred(&fake_power_button_data, -1);
		gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_powerbtn));
	}
}

int fake_pwr_press;

int factory_status(void)
{
	return factory_enable;
}

static void fake_power_button(void)
{
#ifdef CONFIG_BOARD_AZALEA
	if (gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_on_off_btn_l)) == 0 &&
		!fake_pwr_press) {
		fake_pwr_press = 1;
		keyboard_update_button(KEYBOARD_BUTTON_POWER_FAKE,
			fake_pwr_press);
	} else if (gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_on_off_btn_l)) == 1 &&
		fake_pwr_press) {
		fake_pwr_press = 0;
		keyboard_update_button(KEYBOARD_BUTTON_POWER_FAKE,
			fake_pwr_press);
	}
	hook_call_deferred(&fake_power_button_data, 100 * MSEC);
#endif
}
