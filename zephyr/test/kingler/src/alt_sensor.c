/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cros_board_info.h"
#include "cros_cbi.h"
#include "gpio_signal.h"
#include "hooks.h"
#include "zephyr/kernel.h"

#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/ztest.h>

/* SSFC field defined in zephyr/program/corsola/cbi_steelix.dts */
#define SSFC_BASE_MAIN_SENSOR (0x1)
#define SSFC_BASE_ALT_SENSOR (0x1 << 1)

#define SSFC_LID_MAIN_SENSOR (0x1 << 3)
#define SSFC_LID_ALT_SENSOR (0x1 << 4)

#define SSFC_MAIM_SENSORS (SSFC_LID_MAIN_SENSOR | SSFC_BASE_MAIN_SENSOR)
#define SSFC_ALT_SENSORS (SSFC_LID_ALT_SENSOR | SSFC_BASE_ALT_SENSOR)

static void *use_alt_sensor_setup(void)
{
	const struct device *wp_gpio =
		DEVICE_DT_GET(DT_GPIO_CTLR(DT_ALIAS(gpio_wp), gpios));
	const gpio_port_pins_t wp_pin = DT_GPIO_PIN(DT_ALIAS(gpio_wp), gpios);

	/* Make sure that write protect is disabled */
	zassert_ok(gpio_emul_input_set(wp_gpio, wp_pin, 1), NULL);
	/* Set SSFC to enable alt sensors. */
	zassert_ok(cbi_set_ssfc(SSFC_ALT_SENSORS), NULL);
	/* Set form factor to CONVERTIBLE to enable motion sense interrupts. */
	zassert_ok(cbi_set_fw_config(CONVERTIBLE << 13), NULL);
	/* Run init hooks to initialize cbi. */
	hook_notify(HOOK_INIT);

	return NULL;
}

ZTEST_SUITE(use_alt_sensor, NULL, use_alt_sensor_setup, NULL, NULL, NULL);

static void *no_alt_sensor_setup(void)
{
	const struct device *wp_gpio =
		DEVICE_DT_GET(DT_GPIO_CTLR(DT_ALIAS(gpio_wp), gpios));
	const gpio_port_pins_t wp_pin = DT_GPIO_PIN(DT_ALIAS(gpio_wp), gpios);

	/* Make sure that write protect is disabled */
	zassert_ok(gpio_emul_input_set(wp_gpio, wp_pin, 1), NULL);
	/* Set SSFC to disable alt sensors. */
	zassert_ok(cbi_set_ssfc(SSFC_MAIM_SENSORS), NULL);
	/* Set form factor to CONVERTIBLE to enable motion sense interrupts. */
	zassert_ok(cbi_set_fw_config(CONVERTIBLE << 13), NULL);
	/* Run init hooks to initialize cbi. */
	hook_notify(HOOK_INIT);

	return NULL;
}

ZTEST_SUITE(no_alt_sensor, NULL, no_alt_sensor_setup, NULL, NULL, NULL);

static int interrupt_id;

void bmi3xx_interrupt(enum gpio_signal signal)
{
	interrupt_id = 1;
}

void lsm6dsm_interrupt(enum gpio_signal signal)
{
	interrupt_id = 2;
}

ZTEST(use_alt_sensor, test_use_alt_sensor)
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

ZTEST(no_alt_sensor, test_no_alt_sensor)
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
