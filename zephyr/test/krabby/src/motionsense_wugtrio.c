/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cros_board_info.h"
#include "cros_cbi.h"
#include "gpio_signal.h"
#include "hooks.h"
#include "tablet_mode.h"
#include "zephyr/kernel.h"

#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/fff.h>
#include <zephyr/ztest.h>

#define SSFC_LID_MAIN_SENSOR (0x1)
#define SSFC_LID_ALT_SENSOR (0x1 << 1)

FAKE_VALUE_FUNC(int, cbi_get_ssfc, uint32_t *);

static int interrupt_id;
static int ssfc_data;

static int cbi_get_ssfc_mock(uint32_t *ssfc)
{
	*ssfc = ssfc_data;
	return 0;
}

void bmi3xx_interrupt(enum gpio_signal signal)
{
	interrupt_id = 1;
}

void lsm6dsm_interrupt(enum gpio_signal signal)
{
	interrupt_id = 2;
}

static void *alt_sensor_use_setup(void)
{
	const struct device *wp_gpio =
		DEVICE_DT_GET(DT_GPIO_CTLR(DT_ALIAS(gpio_wp), gpios));
	const gpio_port_pins_t wp_pin = DT_GPIO_PIN(DT_ALIAS(gpio_wp), gpios);

	/* Make sure that write protect is disabled */
	zassert_ok(gpio_emul_input_set(wp_gpio, wp_pin, 1), NULL);
	/* Set SSFC to enable alt sensors. */
	cbi_get_ssfc_fake.custom_fake = cbi_get_ssfc_mock;
	ssfc_data = SSFC_LID_ALT_SENSOR;
	/* Run init hooks to initialize cbi. */
	hook_notify(HOOK_INIT);

	return NULL;
}

ZTEST_SUITE(alt_sensor_use, NULL, alt_sensor_use_setup, NULL, NULL, NULL);

ZTEST(alt_sensor_use, test_alt_sensor_use)
{
	const struct device *base_imu_gpio = DEVICE_DT_GET(
		DT_GPIO_CTLR(DT_NODELABEL(base_imu_int_l), gpios));
	const gpio_port_pins_t base_imu_pin =
		DT_GPIO_PIN(DT_NODELABEL(base_imu_int_l), gpios);

	zassert_ok(gpio_emul_input_set(base_imu_gpio, base_imu_pin, 1), NULL);
	k_sleep(K_MSEC(100));
	zassert_ok(gpio_emul_input_set(base_imu_gpio, base_imu_pin, 0), NULL);
	k_sleep(K_MSEC(100));

	zassert_equal(interrupt_id, 2, "interrupt_id=%d", interrupt_id);
}

static void *alt_sensor_no_use_setup(void)
{
	const struct device *wp_gpio =
		DEVICE_DT_GET(DT_GPIO_CTLR(DT_ALIAS(gpio_wp), gpios));
	const gpio_port_pins_t wp_pin = DT_GPIO_PIN(DT_ALIAS(gpio_wp), gpios);

	/* Make sure that write protect is disabled */
	zassert_ok(gpio_emul_input_set(wp_gpio, wp_pin, 1), NULL);
	/* Set SSFC to disable alt sensors. */
	cbi_get_ssfc_fake.custom_fake = cbi_get_ssfc_mock;
	ssfc_data = SSFC_LID_MAIN_SENSOR;
	/* Run init hooks to initialize cbi. */
	hook_notify(HOOK_INIT);

	return NULL;
}

ZTEST_SUITE(alt_sensor_no_use, NULL, alt_sensor_no_use_setup, NULL, NULL, NULL);

ZTEST(alt_sensor_no_use, test_alt_sensor_no_use)
{
	const struct device *base_imu_gpio = DEVICE_DT_GET(
		DT_GPIO_CTLR(DT_NODELABEL(base_imu_int_l), gpios));
	const gpio_port_pins_t base_imu_pin =
		DT_GPIO_PIN(DT_NODELABEL(base_imu_int_l), gpios);

	zassert_ok(gpio_emul_input_set(base_imu_gpio, base_imu_pin, 1), NULL);
	k_sleep(K_MSEC(100));
	zassert_ok(gpio_emul_input_set(base_imu_gpio, base_imu_pin, 0), NULL);
	k_sleep(K_MSEC(100));

	zassert_equal(interrupt_id, 1, "interrupt_id=%d", interrupt_id);
}
