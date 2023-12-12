/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cros_board_info.h"
#include "cros_cbi.h"
#include "gpio_signal.h"
#include "hooks.h"
#include "keyboard_config.h"
#include "keyboard_scan.h"
#include "tablet_mode.h"
#include "zephyr/kernel.h"

#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/fff.h>
#include <zephyr/ztest.h>

/* SSFC field defined in zephyr/program/corsola/cbi_ponyta.dts */
#define SSFC_BASE_MAIN_SENSOR (0x1)
#define SSFC_BASE_ALT_SENSOR (0x1 << 1)

#define SSFC_LID_MAIN_SENSOR (0x1 << 3)
#define SSFC_LID_ALT_SENSOR (0x1 << 4)

static int interrupt_count;
static int interrupt_id;

extern uint8_t board_is_clamshell;

#define SSFC_MAIM_SENSORS (SSFC_LID_MAIN_SENSOR | SSFC_BASE_MAIN_SENSOR)
#define SSFC_ALT_SENSORS (SSFC_LID_ALT_SENSOR | SSFC_BASE_ALT_SENSOR)

/* Vol-up key matrix */
#define VOL_UP_KEY_ROW 1
#define VOL_UP_KEY_COL 5

FAKE_VALUE_FUNC(int, clock_get_freq);
FAKE_VALUE_FUNC(int, cros_cbi_get_fw_config, enum cbi_fw_config_field_id,
		uint32_t *);

int mock_cros_cbi_get_fw_config_clamshell(enum cbi_fw_config_field_id field_id,
					  uint32_t *value)
{
	*value = CLAMSHELL;
	return 0;
}

int mock_cros_cbi_get_fw_config_converible(enum cbi_fw_config_field_id field_id,
					   uint32_t *value)
{
	*value = CONVERTIBLE;
	return 0;
}

int mock_cros_cbi_get_fw_config_error(enum cbi_fw_config_field_id field_id,
				      uint32_t *value)
{
	return -1;
}

static void teardown(void *unused)
{
	/* Reset board globals */
	board_is_clamshell = false;
}

static void *clamshell_setup(void)
{
	int val;

	cros_cbi_get_fw_config_fake.custom_fake =
		mock_cros_cbi_get_fw_config_clamshell;
	hook_notify(HOOK_INIT);

	/* Check if CBI write worked. */
	zassert_ok(cros_cbi_get_fw_config(FORM_FACTOR, &val), NULL);
	zassert_equal(CLAMSHELL, val, "val=%d", val);

	return NULL;
}

ZTEST_SUITE(ponyta_clamshell, NULL, clamshell_setup, NULL, NULL, teardown);

ZTEST(ponyta_clamshell, test_gmr_tablet_switch_disabled)
{
	const struct device *tablet_mode_gpio = DEVICE_DT_GET(
		DT_GPIO_CTLR(DT_NODELABEL(gpio_tablet_mode_l), gpios));
	const gpio_port_pins_t tablet_mode_pin =
		DT_GPIO_PIN(DT_NODELABEL(gpio_tablet_mode_l), gpios);

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

ZTEST(ponyta_clamshell, test_base_imu_irq_disabled)
{
	const struct device *base_imu_gpio = DEVICE_DT_GET(
		DT_GPIO_CTLR(DT_NODELABEL(base_imu_int_l), gpios));
	const gpio_port_pins_t base_imu_pin =
		DT_GPIO_PIN(DT_NODELABEL(base_imu_int_l), gpios);

	/* Verify base_imu_irq is disabled. */
	interrupt_count = 0;
	zassert_ok(gpio_emul_input_set(base_imu_gpio, base_imu_pin, 1), NULL);
	k_sleep(K_MSEC(100));
	zassert_ok(gpio_emul_input_set(base_imu_gpio, base_imu_pin, 0), NULL);
	k_sleep(K_MSEC(100));

	zassert_equal(interrupt_count, 0, "interrupt_count=%d",
		      interrupt_count);
}

ZTEST_USER(ponyta_clamshell, test_error_reading_cbi)
{
	cros_cbi_get_fw_config_fake.custom_fake =
		mock_cros_cbi_get_fw_config_error;
	hook_notify(HOOK_INIT);
}

void bmi3xx_interrupt(enum gpio_signal signal)
{
	interrupt_id = 1;
	interrupt_count++;
}

void lsm6dsm_interrupt(enum gpio_signal signal)
{
	interrupt_id = 2;
}

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
	cros_cbi_get_fw_config_fake.custom_fake =
		mock_cros_cbi_get_fw_config_converible;
	/* Run init hooks to initialize cbi. */
	hook_notify(HOOK_INIT);

	return NULL;
}

ZTEST_SUITE(use_alt_sensor, NULL, use_alt_sensor_setup, NULL, NULL, teardown);

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
	cros_cbi_get_fw_config_fake.custom_fake =
		mock_cros_cbi_get_fw_config_converible;
	/* Run init hooks to initialize cbi. */
	hook_notify(HOOK_INIT);

	return NULL;
}

ZTEST_SUITE(no_alt_sensor, NULL, no_alt_sensor_setup, NULL, NULL, teardown);

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

ZTEST_SUITE(customize_vol_up_key, NULL, NULL, NULL, NULL, teardown);

ZTEST(customize_vol_up_key, test_customize_vol_up_key)
{
	zassert_equal(KEYBOARD_DEFAULT_ROW_VOL_UP, key_vol_up_row);
	zassert_equal(KEYBOARD_DEFAULT_COL_VOL_UP, key_vol_up_col);

	hook_notify(HOOK_INIT);

	zassert_equal(VOL_UP_KEY_ROW, key_vol_up_row);
	zassert_equal(VOL_UP_KEY_COL, key_vol_up_col);
}
