/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Quenbi chipset-specific configuration */

#include "common.h"
#include "gpio.h"
#include "hooks.h"

/* Define delays for deferred power sequencing */
#define HDMI_PWR_ENABLE_DELAY (10 * USEC_PER_MSEC)
#define PP5000_PWR_DISABLE_DELAY (10 * USEC_PER_MSEC)

/*
 * Deferred function to assert gpio_en_hdmi_pwr.
 * This is called on startup to delay enabling HDMI power.
 */
static void hdmi_pwr_enable_deferred(void)
{
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_en_hdmi_pwr), 1);
}
DECLARE_DEFERRED(hdmi_pwr_enable_deferred);

/*
 * Deferred function to deassert gpio_ec_en_pp5000.
 * This is called on shutdown to delay disabling PP5000.
 */
static void pp5000_pwr_disable_deferred(void)
{
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_en_pp5000), 0);
}
DECLARE_DEFERRED(pp5000_pwr_disable_deferred);

void board_chipset_startup_quenbi(void)
{
	/*
	 * Schedule HDMI power to be enabled after a delay,
	 * rather than enabling it immediately, for rising timing requirements.
	 */
	hook_call_deferred(&hdmi_pwr_enable_deferred_data,
			   HDMI_PWR_ENABLE_DELAY);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_usb_en), 1);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_3v_s3_en), 1);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_en_ppvar_oled), 1);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_en_pp5000), 1);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_batt_i2c_en_odl), 1);
}
DECLARE_HOOK(HOOK_CHIPSET_STARTUP, board_chipset_startup_quenbi,
	     HOOK_PRIO_DEFAULT);

void board_chipset_shutdown_quenbi(void)
{
	/*
	 * Quenbi HDMI power loadswitch has active discharge powered by pp5000.
	 * Delay disabling pp5000 to give the loadswitch time to discharge
	 * HDMI Power rails on Quenbi.
	 */
	hook_call_deferred(&pp5000_pwr_disable_deferred_data,
			   PP5000_PWR_DISABLE_DELAY);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_usb_en), 0);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_en_hdmi_pwr), 0);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_3v_s3_en), 0);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_en_ppvar_oled), 0);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_batt_i2c_en_odl), 0);
}
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, board_chipset_shutdown_quenbi,
	     HOOK_PRIO_DEFAULT);
