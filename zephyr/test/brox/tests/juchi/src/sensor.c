/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "gpio/gpio_int.h"
#include "hooks.h"

#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/fff.h>
#include <zephyr/ztest.h>

static void *use_sensor_setup(void)
{
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_lid_imu));

	hook_notify(HOOK_INIT);

	return NULL;
}
ZTEST_SUITE(use_sensor, NULL, use_sensor_setup, NULL, NULL, NULL);

static void *no_sensor_setup(void)
{
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_lid_imu));

	hook_notify(HOOK_INIT);

	return NULL;
}
ZTEST_SUITE(no_sensor, NULL, no_sensor_setup, NULL, NULL, NULL);

ZTEST(use_sensor, test_use_sensor)
{
	const struct device *lid_imu_gpio = DEVICE_DT_GET(
		DT_GPIO_CTLR(DT_NODELABEL(gpio_ec_accel_db_int_l), gpios));
	const gpio_port_pins_t lid_imu_pin =
		DT_GPIO_PIN(DT_NODELABEL(gpio_ec_accel_db_int_l), gpios);

	/* Trigger sensor interrupt */
	zassert_ok(gpio_emul_input_set(lid_imu_gpio, lid_imu_pin, 1), NULL);
	k_sleep(K_MSEC(100));
	zassert_ok(gpio_emul_input_set(lid_imu_gpio, lid_imu_pin, 0), NULL);
	k_sleep(K_MSEC(100));
}

ZTEST(no_sensor, test_no_sensor)
{
	const struct device *lid_imu_gpio = DEVICE_DT_GET(
		DT_GPIO_CTLR(DT_NODELABEL(gpio_ec_accel_db_int_l), gpios));
	const gpio_port_pins_t lid_imu_pin =
		DT_GPIO_PIN(DT_NODELABEL(gpio_ec_accel_db_int_l), gpios);

	/* Trigger sensor interrupt */
	zassert_ok(gpio_emul_input_set(lid_imu_gpio, lid_imu_pin, 1), NULL);
	k_sleep(K_MSEC(100));
	zassert_ok(gpio_emul_input_set(lid_imu_gpio, lid_imu_pin, 0), NULL);
	k_sleep(K_MSEC(100));
}
