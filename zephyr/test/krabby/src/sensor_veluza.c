/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "accelgyro.h"
#include "cros_cbi.h"
#include "driver/accel_bma422.h"
#include "driver/accel_bma4xx.h"
#include "driver/accelgyro_bmi323.h"
#include "gpio/gpio_int.h"
#include "hooks.h"
#include "tablet_mode.h"

#include <zephyr/devicetree.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/adc/adc_emul.h>
#include <zephyr/drivers/emul.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/fff.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/ztest.h>

#include <dt-bindings/buttons.h>
#include <dt-bindings/gpio_defines.h>

FAKE_VALUE_FUNC(int, cros_cbi_get_fw_config, enum cbi_fw_config_field_id,
		uint32_t *);

static int base_interrupt_id;
static int lid_interrupt_id;
static int interrupt_count;

static void test_before(void *fixture)
{
	RESET_FAKE(cros_cbi_get_fw_config);
	base_interrupt_id = 0;
	lid_interrupt_id = 0;
	interrupt_count = 0;
}

ZTEST_SUITE(veluza, NULL, NULL, test_before, NULL, NULL);

void bma4xx_interrupt(enum gpio_signal signal)
{
	lid_interrupt_id = 1;
	interrupt_count++;
}

void bmi3xx_interrupt(enum gpio_signal signal)
{
	base_interrupt_id = 1;
	interrupt_count++;
}

static int fw_factor;

static int
cros_cbi_get_fw_config_fw_factor(enum cbi_fw_config_field_id field_id,
				 uint32_t *value)
{
	if (field_id != FW_FORM_FACTOR)
		return -EINVAL;

	switch (fw_factor) {
	case 0:
		*value = FW_FORM_FACTOR_CLAMSHELL;
		return FW_FORM_FACTOR_CLAMSHELL;
	case 1:
		*value = FW_FORM_FACTOR_CONVERTIBLE;
		return FW_FORM_FACTOR_CONVERTIBLE;
	case -1:
		return -EINVAL;
	default:
		return 0;
	}
	return 0;
}

ZTEST(veluza, test_board_sensor_init)
{
	fw_factor = 0;
	cros_cbi_get_fw_config_fake.custom_fake =
		cros_cbi_get_fw_config_fw_factor;
	hook_notify(HOOK_INIT);

	const struct device *tablet_mode_gpio =
		DEVICE_DT_GET(DT_GPIO_CTLR(DT_NODELABEL(tablet_mode_l), gpios));
	const gpio_port_pins_t tablet_mode_pin =
		DT_GPIO_PIN(DT_NODELABEL(tablet_mode_l), gpios);

	/* Verify gmr_tablet_switch is disabled, by checking the side effects
	 * of calling tablet_set_mode, and setting tablet_mode_l.
	 */
	zassert_ok(gpio_emul_input_set(tablet_mode_gpio, tablet_mode_pin, 0),
		   NULL);
	k_sleep(K_MSEC(100));
	tablet_set_mode(1, TABLET_TRIGGER_LID);
	zassert_equal(0, tablet_get_mode(), NULL);
	zassert_ok(gpio_emul_input_set(tablet_mode_gpio, tablet_mode_pin, 1),
		   NULL);
	k_sleep(K_MSEC(100));
	tablet_set_mode(0, TABLET_TRIGGER_LID);
	zassert_equal(0, tablet_get_mode(), NULL);
	zassert_ok(gpio_emul_input_set(tablet_mode_gpio, tablet_mode_pin, 0),
		   NULL);
	k_sleep(K_MSEC(100));
	tablet_set_mode(1, TABLET_TRIGGER_LID);
	zassert_equal(0, tablet_get_mode(), NULL);

	fw_factor = 1;
	cros_cbi_get_fw_config_fake.custom_fake =
		cros_cbi_get_fw_config_fw_factor;
	hook_notify(HOOK_INIT);
}

ZTEST(veluza, test_base_sensor_interrupt)
{
	const struct device *base_imu_gpio = DEVICE_DT_GET(
		DT_GPIO_CTLR(DT_NODELABEL(base_imu_int_l), gpios));
	const gpio_port_pins_t base_imu_pin =
		DT_GPIO_PIN(DT_NODELABEL(base_imu_int_l), gpios);
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_base_imu));
	interrupt_count = 0;
	base_interrupt_id = 0;
	fw_factor = 1;

	cros_cbi_get_fw_config_fake.custom_fake =
		cros_cbi_get_fw_config_fw_factor;
	zassert_ok(gpio_emul_input_set(base_imu_gpio, base_imu_pin, 1), NULL);
	k_sleep(K_MSEC(100));
	zassert_ok(gpio_emul_input_set(base_imu_gpio, base_imu_pin, 0), NULL);
	k_sleep(K_MSEC(100));
	zassert_equal(base_interrupt_id, 1, "base_interrupt_id=%d",
		      base_interrupt_id);
	zassert_equal(interrupt_count, 1, "unexpected interrupt count: %d",
		      interrupt_count);
}

ZTEST(veluza, test_lid_sensor_interrupt)
{
	const struct device *lid_accel_gpio = DEVICE_DT_GET(
		DT_GPIO_CTLR(DT_NODELABEL(lid_accel_int_l), gpios));
	const gpio_port_pins_t lid_accel_pin =
		DT_GPIO_PIN(DT_NODELABEL(lid_accel_int_l), gpios);
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_lid_imu));
	lid_interrupt_id = 0;
	interrupt_count = 0;
	fw_factor = 1;

	cros_cbi_get_fw_config_fake.custom_fake =
		cros_cbi_get_fw_config_fw_factor;
	zassert_ok(gpio_emul_input_set(lid_accel_gpio, lid_accel_pin, 1), NULL);
	k_sleep(K_MSEC(100));
	zassert_ok(gpio_emul_input_set(lid_accel_gpio, lid_accel_pin, 0), NULL);
	k_sleep(K_MSEC(100));
	zassert_equal(lid_interrupt_id, 1, "base_interrupt_id=%d",
		      lid_interrupt_id);
	zassert_equal(interrupt_count, 1, "unexpected interrupt count: %d",
		      interrupt_count);
}

ZTEST(veluza, test_disable_base_lid_irq)
{
	interrupt_count = 0;
	lid_interrupt_id = 0;
	base_interrupt_id = 0;

	const struct device *base_imu_gpio = DEVICE_DT_GET(
		DT_GPIO_CTLR(DT_NODELABEL(base_imu_int_l), gpios));
	const gpio_port_pins_t base_imu_pin =
		DT_GPIO_PIN(DT_NODELABEL(base_imu_int_l), gpios);
	fw_factor = 0;

	cros_cbi_get_fw_config_fake.custom_fake =
		cros_cbi_get_fw_config_fw_factor;
	hook_notify(HOOK_INIT);

	zassert_ok(gpio_emul_input_set(base_imu_gpio, base_imu_pin, 1), NULL);
	k_sleep(K_MSEC(100));
	zassert_ok(gpio_emul_input_set(base_imu_gpio, base_imu_pin, 0), NULL);
	k_sleep(K_MSEC(100));
	zassert_equal(base_interrupt_id, 0, "base_interrupt_id=%d",
		      base_interrupt_id);
	zassert_equal(interrupt_count, 0, "unexpected interrupt count: %d",
		      interrupt_count);

	const struct device *lid_imu_gpio = DEVICE_DT_GET(
		DT_GPIO_CTLR(DT_NODELABEL(lid_accel_int_l), gpios));
	const gpio_port_pins_t lid_imu_pin =
		DT_GPIO_PIN(DT_NODELABEL(lid_accel_int_l), gpios);

	zassert_ok(gpio_emul_input_set(lid_imu_gpio, lid_imu_pin, 1), NULL);
	k_sleep(K_MSEC(100));
	zassert_ok(gpio_emul_input_set(lid_imu_gpio, lid_imu_pin, 0), NULL);
	k_sleep(K_MSEC(100));

	zassert_equal(lid_interrupt_id, 0, "lid_interrupt_id=%d",
		      lid_interrupt_id);
	zassert_equal(interrupt_count, 0, "unexpected interrupt count: %d",
		      interrupt_count);
}
