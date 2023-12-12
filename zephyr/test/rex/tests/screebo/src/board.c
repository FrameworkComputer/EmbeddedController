/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "gpio.h"
#include "gpio_signal.h"
#include "hooks.h"
#include "usb_charge.h"

#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/fff.h>
#include <zephyr/ztest.h>

ZTEST_SUITE(screebo_board, NULL, NULL, NULL, NULL, NULL);

ZTEST(screebo_board, test_shutdown_process_usba_power)
{
	const struct gpio_dt_spec *gpio =
		GPIO_DT_FROM_NODELABEL(gpio_en_pp5000_usba_r);

	hook_notify(HOOK_CHIPSET_SHUTDOWN);
	zassert_equal(gpio_emul_output_get(gpio->port, gpio->pin), 1);
	k_sleep(K_MSEC(2500));
	zassert_equal(gpio_emul_output_get(gpio->port, gpio->pin), 0);
}

ZTEST(screebo_board, test_bootup_from_s5_process_usba_power)
{
	const struct gpio_dt_spec *gpio =
		GPIO_DT_FROM_NODELABEL(gpio_en_pp5000_usba_r);

	hook_notify(HOOK_CHIPSET_STARTUP);
	zassert_equal(gpio_emul_output_get(gpio->port, gpio->pin), 1);
}

ZTEST(screebo_board, test_bootup_from_g3_process_usba_power)
{
	const struct gpio_dt_spec *gpio =
		GPIO_DT_FROM_NODELABEL(gpio_en_pp5000_usba_r);

	hook_notify(HOOK_CHIPSET_STARTUP);
	zassert_equal(gpio_emul_output_get(gpio->port, gpio->pin), 1);
	hook_notify(HOOK_CHIPSET_SHUTDOWN);
	k_sleep(K_MSEC(1000));
	zassert_equal(gpio_emul_output_get(gpio->port, gpio->pin), 1);
	hook_notify(HOOK_CHIPSET_STARTUP);
	zassert_equal(gpio_emul_output_get(gpio->port, gpio->pin), 1);
}
