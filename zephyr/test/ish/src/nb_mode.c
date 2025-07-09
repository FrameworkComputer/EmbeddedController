/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "gpio.h"
#include "hooks.h"
#include "tablet_mode.h"

#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/ztest.h>

ZTEST(ish_nb_mode, test_nb_mode_low)
{
	const struct gpio_dt_spec *gpio = GPIO_DT_FROM_ALIAS(gpio_nb_mode);

	tablet_set_mode(0, TABLET_TRIGGER_LID);
	hook_notify(HOOK_TABLET_MODE_CHANGE);
	zassert_equal(gpio_emul_output_get(gpio->port, gpio->pin), 0);
}

ZTEST(ish_nb_mode, test_nb_mode_high)
{
	const struct gpio_dt_spec *gpio = GPIO_DT_FROM_ALIAS(gpio_nb_mode);

	tablet_set_mode(1, TABLET_TRIGGER_LID);
	hook_notify(HOOK_TABLET_MODE_CHANGE);
	zassert_equal(gpio_emul_output_get(gpio->port, gpio->pin), 1);
}

ZTEST_SUITE(ish_nb_mode, NULL, NULL, NULL, NULL, NULL);
