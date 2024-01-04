/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "gpio_signal.h"
#include "hooks.h"
#include "zephyr/kernel.h"

#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/fff.h>
#include <zephyr/ztest.h>

FAKE_VOID_FUNC(x_ec_interrupt);

static int interrupt_id;

static void *kyogre_sensor_setup(void)
{
	hook_notify(HOOK_INIT);

	return NULL;
}

void lsm6dso_interrupt(enum gpio_signal signal)
{
	interrupt_id = 1;
}

void lis2dw12_interrupt(enum gpio_signal signal)
{
	interrupt_id = 2;
}

ZTEST_SUITE(main_sensor, NULL, kyogre_sensor_setup, NULL, NULL, NULL);

ZTEST(main_sensor, test_main_sensor)
{
	/* lsm6dso_interrupt test*/
	const struct device *base_imu_gpio = DEVICE_DT_GET(
		DT_GPIO_CTLR(DT_NODELABEL(base_imu_int_l), gpios));
	const gpio_port_pins_t base_imu_pin =
		DT_GPIO_PIN(DT_NODELABEL(base_imu_int_l), gpios);

	zassert_ok(gpio_emul_input_set(base_imu_gpio, base_imu_pin, 1), NULL);
	k_sleep(K_MSEC(100));
	zassert_ok(gpio_emul_input_set(base_imu_gpio, base_imu_pin, 0), NULL);
	k_sleep(K_MSEC(100));

	zassert_equal(interrupt_id, 1, "interrupt_id=%d", interrupt_id);

	/* lis2dw12_interrupt test */
	const struct device *lid_accel_gpio = DEVICE_DT_GET(
		DT_GPIO_CTLR(DT_NODELABEL(lid_accel_int_l), gpios));
	const gpio_port_pins_t lid_accel_pin =
		DT_GPIO_PIN(DT_NODELABEL(lid_accel_int_l), gpios);

	zassert_ok(gpio_emul_input_set(lid_accel_gpio, lid_accel_pin, 1), NULL);
	k_sleep(K_MSEC(100));
	zassert_ok(gpio_emul_input_set(lid_accel_gpio, lid_accel_pin, 0), NULL);
	k_sleep(K_MSEC(100));

	zassert_equal(interrupt_id, 2, "interrupt_id=%d", interrupt_id);
}
