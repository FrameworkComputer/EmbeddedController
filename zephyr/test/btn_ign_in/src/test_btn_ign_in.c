/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "include/power_button.h"

#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/ztest.h>

static void assert_btn_ign(void)
{
	const struct gpio_dt_spec *gp_btn_ign =
		GPIO_DT_FROM_NODELABEL(gpio_btn_ign);

	zassert_ok(gpio_emul_input_set_dt(gp_btn_ign, 1),
		   "Failed to set BTN_IGN high");
}

static void deassert_btn_ign(void)
{
	const struct gpio_dt_spec *gp_btn_ign =
		GPIO_DT_FROM_NODELABEL(gpio_btn_ign);

	zassert_ok(gpio_emul_input_set_dt(gp_btn_ign, 0),
		   "Failed to set BTN_IGN low");
}

ZTEST(btn_ign_in, test_btn_ign_not_set)
{
	deassert_btn_ign();
	power_button_simulate_press(10000);
	zassert_equal(1, power_button_is_pressed(),
		      "Power button should not be ignored after 0s");
	k_sleep(K_SECONDS(1));
	zassert_equal(1, power_button_is_pressed(),
		      "Power button should not be ignored after 1s");
	k_sleep(K_SECONDS(4));
	zassert_equal(1, power_button_is_pressed(),
		      "Power button should not be ignored after 5s");
}

ZTEST(btn_ign_in, test_btn_ign_set)
{
	assert_btn_ign();
	power_button_simulate_press(10000);
	zassert_equal(0, power_button_is_pressed(),
		      "Power button should be ignored after 0s");
	k_sleep(K_SECONDS(3));
	zassert_equal(0, power_button_is_pressed(),
		      "Power button should be ignored after 3s");
	k_sleep(K_SECONDS(2));
	zassert_equal(1, power_button_is_pressed(),
		      "Power button should not be ignored after 5s");
}

ZTEST_SUITE(btn_ign_in, NULL, NULL, NULL, NULL, NULL);
