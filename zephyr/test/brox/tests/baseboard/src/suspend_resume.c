/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "system.h"

#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/ztest.h>

#include <ap_power/ap_power.h>
#include <ap_power/ap_power_events.h>

ZTEST_SUITE(suspend_resume, NULL, NULL, NULL, NULL, NULL);

ZTEST(suspend_resume, test_suspend_resume_handler)
{
	const struct gpio_dt_spec *gpio_ec_en_pp3300_wlan =
		GPIO_DT_FROM_NODELABEL(gpio_ec_en_pp3300_wlan);
	const struct gpio_dt_spec *gpio_amp_mute_l =
		GPIO_DT_FROM_NODELABEL(gpio_amp_mute_l);

	gpio_pin_set_dt(gpio_ec_en_pp3300_wlan, 1);
	gpio_pin_set_dt(gpio_amp_mute_l, 0);
	ap_power_ev_send_callbacks(AP_POWER_HARD_OFF);
	zassert_false(gpio_emul_output_get(gpio_ec_en_pp3300_wlan->port,
					   gpio_ec_en_pp3300_wlan->pin));
	/* GPIO emulator always returns physical state of the pin */
	zassert_false(gpio_emul_output_get(gpio_amp_mute_l->port,
					   gpio_amp_mute_l->pin));

	gpio_pin_set_dt(gpio_ec_en_pp3300_wlan, 0);
	gpio_pin_set_dt(gpio_amp_mute_l, 1);
	ap_power_ev_send_callbacks(AP_POWER_PRE_INIT);
	zassert_true(gpio_emul_output_get(gpio_ec_en_pp3300_wlan->port,
					  gpio_ec_en_pp3300_wlan->pin));
	zassert_true(gpio_emul_output_get(gpio_amp_mute_l->port,
					  gpio_amp_mute_l->pin));

	gpio_pin_set_dt(gpio_ec_en_pp3300_wlan, 0);
	gpio_pin_set_dt(gpio_amp_mute_l, 1);
	ap_power_ev_send_callbacks(AP_POWER_STARTUP);
	zassert_true(gpio_emul_output_get(gpio_ec_en_pp3300_wlan->port,
					  gpio_ec_en_pp3300_wlan->pin));
	zassert_true(gpio_emul_output_get(gpio_amp_mute_l->port,
					  gpio_amp_mute_l->pin));
}
