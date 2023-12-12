/* Copyright 2022 The ChromiumOS Authors
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
#include <zephyr/ztest.h>

#define BOARD_CLAMSHELL (0 << 7)
#define BOARD_CONVERTIBLE (1 << 7)
#define MAIN_BASE_SENSOR (1 << 8)
#define ALT_BASE_SENSOR (2 << 8)
#define MAIN_LID_SENSOR (1 << 10)
#define ALT_LID_SENSOR (2 << 10)

#define MAIN_FWCONFIG (BOARD_CONVERTIBLE | MAIN_BASE_SENSOR | MAIN_LID_SENSOR)
#define ALT_FWCONFIG (BOARD_CONVERTIBLE | ALT_BASE_SENSOR | ALT_LID_SENSOR)

static int base_interrupt_id;
static int lid_interrupt_id;

extern uint8_t base_is_none;
extern uint8_t lid_is_none;

static void teardown(void *unused)
{
	/* Reset board globals */
	base_is_none = false;
	lid_is_none = false;
}

static void *clamshell_setup(void)
{
	uint32_t val;
	const struct device *wp_gpio =
		DEVICE_DT_GET(DT_GPIO_CTLR(DT_ALIAS(gpio_wp), gpios));
	const gpio_port_pins_t wp_pin = DT_GPIO_PIN(DT_ALIAS(gpio_wp), gpios);

	base_interrupt_id = 0;
	lid_interrupt_id = 0;

	/* Make sure that write protect is disabled */
	zassert_ok(gpio_emul_input_set(wp_gpio, wp_pin, 1), NULL);
	/* Set CBI to BOARD_CLAMSHELL */
	zassert_ok(cbi_set_fw_config(BOARD_CLAMSHELL), NULL);
	/* Run init hooks to initialize cbi. */
	hook_notify(HOOK_INIT);

	/* Check if CBI write worked. */
	zassert_ok(cros_cbi_get_fw_config(FORM_FACTOR, &val), NULL);
	zassert_equal(CLAMSHELL, val, "val=%d", val);
	zassert_ok(cros_cbi_get_fw_config(BASE_SENSOR, &val), NULL);
	zassert_equal(BASE_NONE, val, "val=%d", val);
	zassert_ok(cros_cbi_get_fw_config(LID_SENSOR, &val), NULL);
	zassert_equal(LID_NONE, val, "val=%d", val);

	return NULL;
}

ZTEST_SUITE(tentacruel_clamshell, NULL, clamshell_setup, NULL, NULL, teardown);

static void *main_sensor_setup(void)
{
	uint32_t val;
	const struct device *wp_gpio =
		DEVICE_DT_GET(DT_GPIO_CTLR(DT_ALIAS(gpio_wp), gpios));
	const gpio_port_pins_t wp_pin = DT_GPIO_PIN(DT_ALIAS(gpio_wp), gpios);

	base_interrupt_id = 0;
	lid_interrupt_id = 0;

	/* Make sure that write protect is disabled */
	zassert_ok(gpio_emul_input_set(wp_gpio, wp_pin, 1), NULL);
	/* Set CBI to main sensor config */
	zassert_ok(cbi_set_fw_config(MAIN_FWCONFIG), NULL);
	/* Run init hooks to initialize cbi. */
	hook_notify(HOOK_INIT);

	/* Check if CBI write worked. */
	zassert_ok(cros_cbi_get_fw_config(FORM_FACTOR, &val), NULL);
	zassert_equal(CONVERTIBLE, val, "val=%d", val);
	zassert_ok(cros_cbi_get_fw_config(BASE_SENSOR, &val), NULL);
	zassert_equal(BASE_ICM42607, val, "val=%d", val);
	zassert_ok(cros_cbi_get_fw_config(LID_SENSOR, &val), NULL);
	zassert_equal(LID_LIS2DWLTR, val, "val=%d", val);

	return NULL;
}

ZTEST_SUITE(tentacruel_main_sensor, NULL, main_sensor_setup, NULL, NULL,
	    teardown);

static void *alt_sensor_setup(void)
{
	uint32_t val;
	const struct device *wp_gpio =
		DEVICE_DT_GET(DT_GPIO_CTLR(DT_ALIAS(gpio_wp), gpios));
	const gpio_port_pins_t wp_pin = DT_GPIO_PIN(DT_ALIAS(gpio_wp), gpios);

	base_interrupt_id = 0;
	lid_interrupt_id = 0;

	/* Make sure that write protect is disabled */
	zassert_ok(gpio_emul_input_set(wp_gpio, wp_pin, 1), NULL);
	/* Set CBI to alt sensor config */
	zassert_ok(cbi_set_fw_config(ALT_FWCONFIG), NULL);
	/* Run init hooks to initialize cbi. */
	hook_notify(HOOK_INIT);

	/* Check if CBI write worked. */
	zassert_ok(cros_cbi_get_fw_config(FORM_FACTOR, &val), NULL);
	zassert_equal(CONVERTIBLE, val, "val=%d", val);
	zassert_ok(cros_cbi_get_fw_config(BASE_SENSOR, &val), NULL);
	zassert_equal(BASE_BMI323, val, "val=%d", val);
	zassert_ok(cros_cbi_get_fw_config(LID_SENSOR, &val), NULL);
	zassert_equal(LID_BMA422, val, "val=%d", val);

	return NULL;
}

ZTEST_SUITE(tentacruel_alt_sensor, NULL, alt_sensor_setup, NULL, NULL,
	    teardown);

/* Main gyro sensor. */
void icm42607_interrupt(enum gpio_signal signal)
{
	base_interrupt_id = 1;
}

void bmi3xx_interrupt(enum gpio_signal signal)
{
	base_interrupt_id = 2;
}

/* Main lid sensor. */
void lis2dw12_interrupt(enum gpio_signal signal)
{
	lid_interrupt_id = 1;
}

void bma4xx_interrupt(enum gpio_signal signal)
{
	lid_interrupt_id = 2;
}

ZTEST(tentacruel_clamshell, test_tabletmode_disable)
{
	const struct device *tablet_mode_gpio =
		DEVICE_DT_GET(DT_GPIO_CTLR(DT_NODELABEL(tablet_mode_l), gpios));
	const gpio_port_pins_t tablet_mode_pin =
		DT_GPIO_PIN(DT_NODELABEL(tablet_mode_l), gpios);

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

ZTEST(tentacruel_clamshell, test_irq_disable)
{
	const struct device *base_imu_gpio = DEVICE_DT_GET(
		DT_GPIO_CTLR(DT_NODELABEL(base_imu_int_l), gpios));
	const gpio_port_pins_t base_imu_pin =
		DT_GPIO_PIN(DT_NODELABEL(base_imu_int_l), gpios);

	zassert_ok(gpio_emul_input_set(base_imu_gpio, base_imu_pin, 1), NULL);
	k_sleep(K_MSEC(100));
	zassert_ok(gpio_emul_input_set(base_imu_gpio, base_imu_pin, 0), NULL);
	k_sleep(K_MSEC(100));

	zassert_equal(base_interrupt_id, 0, "base_interrupt_id=%d",
		      base_interrupt_id);

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
}

ZTEST(tentacruel_main_sensor, test_tentacruel_main_sensor)
{
	const struct device *base_imu_gpio = DEVICE_DT_GET(
		DT_GPIO_CTLR(DT_NODELABEL(base_imu_int_l), gpios));
	const gpio_port_pins_t base_imu_pin =
		DT_GPIO_PIN(DT_NODELABEL(base_imu_int_l), gpios);

	zassert_ok(gpio_emul_input_set(base_imu_gpio, base_imu_pin, 1), NULL);
	k_sleep(K_MSEC(100));
	zassert_ok(gpio_emul_input_set(base_imu_gpio, base_imu_pin, 0), NULL);
	k_sleep(K_MSEC(100));

	zassert_equal(base_interrupt_id, 1, "base_interrupt_id=%d",
		      base_interrupt_id);

	const struct device *lid_imu_gpio = DEVICE_DT_GET(
		DT_GPIO_CTLR(DT_NODELABEL(lid_accel_int_l), gpios));
	const gpio_port_pins_t lid_imu_pin =
		DT_GPIO_PIN(DT_NODELABEL(lid_accel_int_l), gpios);

	zassert_ok(gpio_emul_input_set(lid_imu_gpio, lid_imu_pin, 1), NULL);
	k_sleep(K_MSEC(100));
	zassert_ok(gpio_emul_input_set(lid_imu_gpio, lid_imu_pin, 0), NULL);
	k_sleep(K_MSEC(100));

	zassert_equal(lid_interrupt_id, 1, "lid_interrupt_id=%d",
		      lid_interrupt_id);
}

ZTEST(tentacruel_alt_sensor, test_tentacruel_alt_sensor)
{
	const struct device *base_imu_gpio = DEVICE_DT_GET(
		DT_GPIO_CTLR(DT_NODELABEL(base_imu_int_l), gpios));
	const gpio_port_pins_t base_imu_pin =
		DT_GPIO_PIN(DT_NODELABEL(base_imu_int_l), gpios);

	zassert_ok(gpio_emul_input_set(base_imu_gpio, base_imu_pin, 1), NULL);
	k_sleep(K_MSEC(100));
	zassert_ok(gpio_emul_input_set(base_imu_gpio, base_imu_pin, 0), NULL);
	k_sleep(K_MSEC(100));

	zassert_equal(base_interrupt_id, 2, "base_interrupt_id=%d",
		      base_interrupt_id);

	const struct device *lid_imu_gpio = DEVICE_DT_GET(
		DT_GPIO_CTLR(DT_NODELABEL(lid_accel_int_l), gpios));
	const gpio_port_pins_t lid_imu_pin =
		DT_GPIO_PIN(DT_NODELABEL(lid_accel_int_l), gpios);

	zassert_ok(gpio_emul_input_set(lid_imu_gpio, lid_imu_pin, 1), NULL);
	k_sleep(K_MSEC(100));
	zassert_ok(gpio_emul_input_set(lid_imu_gpio, lid_imu_pin, 0), NULL);
	k_sleep(K_MSEC(100));

	zassert_equal(lid_interrupt_id, 2, "lid_interrupt_id=%d",
		      lid_interrupt_id);
}
