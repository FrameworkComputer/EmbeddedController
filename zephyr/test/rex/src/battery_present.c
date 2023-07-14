/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "battery.h"

#include <zephyr/device.h>
#include <zephyr/drivers/emul.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/fff.h>
#include <zephyr/ztest.h>

#define GPIO_BATT_PRES_ODL_PATH NAMED_GPIOS_GPIO_NODE(ec_batt_pres_odl)
#define GPIO_BATT_PRES_ODL_PORT DT_GPIO_PIN(GPIO_BATT_PRES_ODL_PATH, gpios)

FAKE_VALUE_FUNC(int, board_set_active_charge_port, int);

static void battery_after(void *data)
{
	const struct device *dev =
		DEVICE_DT_GET(DT_GPIO_CTLR(GPIO_BATT_PRES_ODL_PATH, gpios));

	/* Set default state (battery is present) */
	gpio_emul_input_set(dev, GPIO_BATT_PRES_ODL_PORT, 0);
}

ZTEST_SUITE(rex_battery, NULL, NULL, NULL, battery_after, NULL);

static bool mock_battery_cutoff_state;

int battery_is_cut_off(void)
{
	return mock_battery_cutoff_state;
}

ZTEST_USER(rex_battery, test_battery_is_present)
{
	const struct device *dev =
		DEVICE_DT_GET(DT_GPIO_CTLR(GPIO_BATT_PRES_ODL_PATH, gpios));

	zassert_not_null(dev, NULL);

	mock_battery_cutoff_state = true;
	zassert_ok(gpio_emul_input_set(dev, GPIO_BATT_PRES_ODL_PORT, 0));
	zassert_equal(BP_NO, battery_is_present());

	mock_battery_cutoff_state = true;
	zassert_ok(gpio_emul_input_set(dev, GPIO_BATT_PRES_ODL_PORT, 1));
	zassert_equal(BP_NO, battery_is_present());

	mock_battery_cutoff_state = false;
	zassert_ok(gpio_emul_input_set(dev, GPIO_BATT_PRES_ODL_PORT, 0));
	zassert_equal(BP_YES, battery_is_present());

	mock_battery_cutoff_state = false;
	zassert_ok(gpio_emul_input_set(dev, GPIO_BATT_PRES_ODL_PORT, 1));
	zassert_equal(BP_NO, battery_is_present());
}

ZTEST_USER(rex_battery, test_battery_hw_present)
{
	const struct device *dev =
		DEVICE_DT_GET(DT_GPIO_CTLR(GPIO_BATT_PRES_ODL_PATH, gpios));

	zassert_not_null(dev, NULL);

	zassert_ok(gpio_emul_input_set(dev, GPIO_BATT_PRES_ODL_PORT, 0));
	zassert_equal(BP_YES, battery_hw_present());

	zassert_ok(gpio_emul_input_set(dev, GPIO_BATT_PRES_ODL_PORT, 1));
	zassert_equal(BP_NO, battery_hw_present());
}
