/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/fff.h>
#include <zephyr/ztest.h>

#include <driver/amd_stb.h>
#include <gpio.h>
#include <gpio/gpio_int.h>

ZTEST_SUITE(stb_dump, NULL, NULL, NULL, NULL, NULL);

ZTEST(stb_dump, test_stb_dump)
{
	const struct gpio_dt_spec *ec_sfh_int =
		GPIO_DT_FROM_NODELABEL(gpio_ec_sfh_int_h);
	const struct gpio_dt_spec *sfh_ec_int =
		GPIO_DT_FROM_NODELABEL(gpio_sfh_ec_int_h);
	int rv;

	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_stb_dump));
	amd_stb_dump_init(ec_sfh_int, sfh_ec_int);

	amd_stb_dump_trigger();
	rv = gpio_emul_output_get(ec_sfh_int->port, ec_sfh_int->pin);
	zassert_equal(rv, 1);
	zassert_true(amd_stb_dump_in_progress());

	rv = gpio_emul_input_set(sfh_ec_int->port, sfh_ec_int->pin, true);
	zassert_ok(rv);
	/* Give the interrupt handler plenty of time to run. */
	k_msleep(10);
	zassert_false(amd_stb_dump_in_progress());
	rv = gpio_emul_output_get(ec_sfh_int->port, ec_sfh_int->pin);
	zassert_equal(rv, 0);
}
