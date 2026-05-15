/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Mensa chipset-specific configuration */

#include "common.h"
#include "gpio.h"
#include "hooks.h"

/* Define delays for deferred power sequencing */
#define PP5000_PWR_DISABLE_DELAY (10 * USEC_PER_MSEC)

/*
 * Deferred function to deassert gpio_ec_en_pp5000.
 * This is called on shutdown to delay disabling PP5000.
 */
static void pp5000_pwr_disable_deferred(void)
{
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_en_pp5000), 0);
}
DECLARE_DEFERRED(pp5000_pwr_disable_deferred);

void board_chipset_startup_mensa(void)
{
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_en_usba), 1);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_en_pp3300_s3), 1);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_en_ppvar_oled), 1);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_en_pp5000), 1);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_en_pp5000_fan), 1);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_en_pp5000_led_x), 1);
}
DECLARE_HOOK(HOOK_CHIPSET_STARTUP, board_chipset_startup_mensa,
	     HOOK_PRIO_DEFAULT);

void board_chipset_shutdown_mensa(void)
{
	/*
	 * Mensa HDMI power loadswitch has active discharge powered by pp5000.
	 * Delay disabling pp5000 to give the loadswitch time to discharge
	 * HDMI Power rails on Mensa.
	 */
	hook_call_deferred(&pp5000_pwr_disable_deferred_data,
			   PP5000_PWR_DISABLE_DELAY);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_en_usba), 0);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_en_pp3300_s3), 0);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_en_ppvar_oled), 0);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_en_pp5000_fan), 0);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_en_pp5000_led_x), 0);
}
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, board_chipset_shutdown_mensa,
	     HOOK_PRIO_DEFAULT);
