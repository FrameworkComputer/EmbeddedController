/* Copyright 2023 The ChromiumOS Authors
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

void clamshell_init(void);

FAKE_VALUE_FUNC(int, cros_cbi_get_fw_config, enum cbi_fw_config_field_id,
		uint32_t *);

static bool clamshell_mode;
static int interrupt_count;

void bmi3xx_interrupt(enum gpio_signal signal)
{
	interrupt_count++;
}

static int cros_cbi_get_fw_config_mock(enum cbi_fw_config_field_id field_id,
				       uint32_t *value)
{
	if (field_id != FW_FORM_FACTOR)
		return -EINVAL;

	*value = clamshell_mode ? FW_FF_CLAMSHELL : FW_FF_CONVERTIBLE;
	return 0;
}

static void *form_factor_setup(void)
{
	const struct device *tablet_mode_gpio =
		DEVICE_DT_GET(DT_GPIO_CTLR(DT_NODELABEL(tablet_mode_l), gpios));
	const gpio_port_pins_t tablet_mode_pin =
		DT_GPIO_PIN(DT_NODELABEL(tablet_mode_l), gpios);

	/* Set default value of TABLET_MODE_L */
	gpio_emul_input_set(tablet_mode_gpio, tablet_mode_pin, 1);

	clamshell_mode = 0;

	RESET_FAKE(cros_cbi_get_fw_config);
	cros_cbi_get_fw_config_fake.custom_fake = cros_cbi_get_fw_config_mock;

	hook_notify(HOOK_INIT);

	return NULL;
}

ZTEST_SUITE(skyrim_form_factor, NULL, form_factor_setup, NULL, NULL, NULL);

ZTEST(skyrim_form_factor, test_03_clamshell_gmr_tablet_switch_disabled)
{
	const struct device *tablet_mode_gpio =
		DEVICE_DT_GET(DT_GPIO_CTLR(DT_NODELABEL(tablet_mode_l), gpios));
	const gpio_port_pins_t tablet_mode_pin =
		DT_GPIO_PIN(DT_NODELABEL(tablet_mode_l), gpios);

	clamshell_mode = 1;
	clamshell_init();

	/* Verify gmr_tablet_switch is disabled, by checking the side effects
	 * of calling tablet_set_mode, and setting gpio_tablet_mode_l.
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
}

ZTEST(skyrim_form_factor, test_04_clamshell_base_imu_irq_disabled)
{
	const struct device *base_imu_gpio = DEVICE_DT_GET(
		DT_GPIO_CTLR(DT_NODELABEL(gpio_accel_gyro_int_l), gpios));
	const gpio_port_pins_t base_imu_pin =
		DT_GPIO_PIN(DT_NODELABEL(gpio_accel_gyro_int_l), gpios);

	clamshell_mode = 1;
	clamshell_init();

	/* Verify base_imu_irq is disabled. */
	interrupt_count = 0;
	zassert_ok(gpio_emul_input_set(base_imu_gpio, base_imu_pin, 1), NULL);
	k_sleep(K_MSEC(100));
	zassert_ok(gpio_emul_input_set(base_imu_gpio, base_imu_pin, 0), NULL);
	k_sleep(K_MSEC(100));

	zassert_equal(interrupt_count, 0, "interrupt_count=%d",
		      interrupt_count);
}

ZTEST(skyrim_form_factor, test_01_convertible_gmr_tablet_switch_enabled)
{
	const struct device *tablet_mode_gpio =
		DEVICE_DT_GET(DT_GPIO_CTLR(DT_NODELABEL(tablet_mode_l), gpios));
	const gpio_port_pins_t tablet_mode_pin =
		DT_GPIO_PIN(DT_NODELABEL(tablet_mode_l), gpios);

	clamshell_mode = 0;
	clamshell_init();

	/* Verify gmr_tablet_switch is enabled, by checking the side effects
	 * of calling tablet_set_mode, and setting gpio_tablet_mode_l.
	 */
	zassert_ok(gpio_emul_input_set(tablet_mode_gpio, tablet_mode_pin, 0),
		   NULL);
	k_sleep(K_MSEC(100));
	tablet_set_mode(1, TABLET_TRIGGER_LID);
	zassert_equal(1, tablet_get_mode(), NULL);
	zassert_ok(gpio_emul_input_set(tablet_mode_gpio, tablet_mode_pin, 1),
		   NULL);
	k_sleep(K_MSEC(100));
	tablet_set_mode(0, TABLET_TRIGGER_LID);
	zassert_equal(0, tablet_get_mode(), NULL);
	zassert_ok(gpio_emul_input_set(tablet_mode_gpio, tablet_mode_pin, 0),
		   NULL);
	k_sleep(K_MSEC(100));
	tablet_set_mode(1, TABLET_TRIGGER_LID);
	zassert_equal(1, tablet_get_mode(), NULL);
}

ZTEST(skyrim_form_factor, test_02_convertible_base_imu_irq_enabled)
{
	const struct device *base_imu_gpio = DEVICE_DT_GET(
		DT_GPIO_CTLR(DT_NODELABEL(gpio_accel_gyro_int_l), gpios));
	const gpio_port_pins_t base_imu_pin =
		DT_GPIO_PIN(DT_NODELABEL(gpio_accel_gyro_int_l), gpios);

	clamshell_mode = 0;
	clamshell_init();

	/* Verify base_imu_irq is enabled. Interrupt is configured
	 * GPIO_INT_EDGE_FALLING, so set high, then set low.
	 */
	interrupt_count = 0;
	zassert_ok(gpio_emul_input_set(base_imu_gpio, base_imu_pin, 1), NULL);
	k_sleep(K_MSEC(100));
	zassert_ok(gpio_emul_input_set(base_imu_gpio, base_imu_pin, 0), NULL);
	k_sleep(K_MSEC(100));

	zassert_equal(interrupt_count, 1, "interrupt_count=%d",
		      interrupt_count);
}
