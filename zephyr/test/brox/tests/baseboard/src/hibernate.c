/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "system.h"

#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/fff.h>
#include <zephyr/ztest.h>

FAKE_VALUE_FUNC(enum ec_error_list, isl9238c_hibernate, int);

ZTEST_SUITE(hibernate, NULL, NULL, NULL, NULL, NULL);

ZTEST(hibernate, test_board_get_pd_port_location)
{
	const struct gpio_dt_spec *gpio_en_slp_z =
		GPIO_DT_FROM_NODELABEL(gpio_en_slp_z);

	gpio_pin_set_dt(gpio_en_slp_z, 0);
	board_hibernate_late();
	zassert_true(
		gpio_emul_output_get(gpio_en_slp_z->port, gpio_en_slp_z->pin));
}

ZTEST(hibernate, test_board_hibernate)
{
	RESET_FAKE(isl9238c_hibernate);

	board_hibernate();
	zassert_equal(isl9238c_hibernate_fake.call_count, 1);
	zassert_equal(isl9238c_hibernate_fake.arg0_val, 0);
}
