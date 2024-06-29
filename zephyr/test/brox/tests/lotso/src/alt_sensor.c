/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cros_cbi.h"
#include "gpio/gpio_int.h"
#include "hooks.h"

#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/fff.h>
#include <zephyr/ztest.h>

FAKE_VOID_FUNC(motion_sensors_check_ssfc);
FAKE_VALUE_FUNC(bool, cros_cbi_ssfc_check_match, enum cbi_ssfc_value_id);

static int mock_cros_cbi_ssfc_check_match(enum cbi_ssfc_value_id ssfc)
{
	return true;
}

static int mock_cros_cbi_ssfc_check_not_match(enum cbi_ssfc_value_id ssfc)
{
	return false;
}

static void *use_alt_sensor_setup(void)
{
	RESET_FAKE(motion_sensors_check_ssfc);
	cros_cbi_ssfc_check_match_fake.custom_fake =
		mock_cros_cbi_ssfc_check_match;
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_lid_imu));

	hook_notify(HOOK_INIT);

	return NULL;
}
ZTEST_SUITE(use_alt_sensor, NULL, use_alt_sensor_setup, NULL, NULL, NULL);

static void *no_alt_sensor_setup(void)
{
	RESET_FAKE(motion_sensors_check_ssfc);
	cros_cbi_ssfc_check_match_fake.custom_fake =
		mock_cros_cbi_ssfc_check_not_match;
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_lid_imu));

	hook_notify(HOOK_INIT);

	return NULL;
}
ZTEST_SUITE(no_alt_sensor, NULL, no_alt_sensor_setup, NULL, NULL, NULL);

static int interrupt_id;

void bma4xx_interrupt(enum gpio_signal signal)
{
	interrupt_id = 1;
}

void lis2dw12_interrupt(enum gpio_signal signal)
{
	interrupt_id = 2;
}

ZTEST(use_alt_sensor, test_use_alt_sensor)
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

	zassert_equal(interrupt_id, 2);
	zassert_equal(motion_sensors_check_ssfc_fake.call_count, 1);
}

ZTEST(no_alt_sensor, test_no_alt_sensor)
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

	zassert_equal(interrupt_id, 1);
	zassert_equal(motion_sensors_check_ssfc_fake.call_count, 1);
}
