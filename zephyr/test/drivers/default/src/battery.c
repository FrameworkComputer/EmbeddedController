/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/gpio/gpio_emul.h>

#include "battery.h"
#include "test/drivers/test_state.h"

#define GPIO_BATT_PRES_ODL_PATH DT_PATH(named_gpios, ec_batt_pres_odl)
#define GPIO_BATT_PRES_ODL_PORT DT_GPIO_PIN(GPIO_BATT_PRES_ODL_PATH, gpios)

static void battery_after(void *data)
{
	const struct device *dev =
		DEVICE_DT_GET(DT_GPIO_CTLR(GPIO_BATT_PRES_ODL_PATH, gpios));

	/* Set default state (battery is present) */
	gpio_emul_input_set(dev, GPIO_BATT_PRES_ODL_PORT, 0);
}

ZTEST_USER(battery, test_battery_is_present_gpio)
{
	const struct device *dev =
		DEVICE_DT_GET(DT_GPIO_CTLR(GPIO_BATT_PRES_ODL_PATH, gpios));

	zassert_not_null(dev, NULL);
	/* ec_batt_pres_odl = 0 means battery present. */
	zassert_ok(gpio_emul_input_set(dev, GPIO_BATT_PRES_ODL_PORT, 0), NULL);
	zassert_equal(BP_YES, battery_is_present(), NULL);
	/* ec_batt_pres_odl = 1 means battery missing. */
	zassert_ok(gpio_emul_input_set(dev, GPIO_BATT_PRES_ODL_PORT, 1), NULL);
	zassert_equal(BP_NO, battery_is_present(), NULL);
}

ZTEST_SUITE(battery, drivers_predicate_post_main, NULL, NULL, battery_after,
	    NULL);
