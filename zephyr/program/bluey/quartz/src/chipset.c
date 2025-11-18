/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Quartz chipset-specific configuration */

#include "common.h"
#include "gpio.h"
#include "hooks.h"

void board_chipset_startup_quartz(void)
{
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_haptic_en_ec), 1);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_tpad_en), 1);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_bl_off_odl), 1);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_en_pp5000_fan), 1);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_en_pp3300_s3), 1);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_enavdd_oled), 1);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_en_pp5000_s5), 1);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_en_pp5000_led_x), 1);
}
DECLARE_HOOK(HOOK_CHIPSET_STARTUP, board_chipset_startup_quartz,
	     HOOK_PRIO_DEFAULT);

void board_chipset_shutdown_quartz(void)
{
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_haptic_en_ec), 0);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_tpad_en), 0);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_bl_off_odl), 0);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_en_pp5000_fan), 0);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_en_pp3300_s3), 0);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_enavdd_oled), 0);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_en_pp5000_s5), 0);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_en_pp5000_led_x), 0);
}
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, board_chipset_shutdown_quartz,
	     HOOK_PRIO_DEFAULT);
