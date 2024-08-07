/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "battery.h"
#include "cros_board_info.h"
#include "cros_cbi.h"
#include "gpio/gpio_int.h"
#include "hooks.h"
#include "keyboard_8042_sharedlib.h"
#include "motionsense_sensors.h"
#include "tablet_mode.h"
#include "teliks.h"

#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/fff.h>
#include <zephyr/logging/log.h>
#include <zephyr/ztest.h>

#include <dt-bindings/gpio_defines.h>

LOG_MODULE_REGISTER(nissa, LOG_LEVEL_INF);

FAKE_VALUE_FUNC(int, cbi_get_ssfc, uint32_t *);
FAKE_VALUE_FUNC(int, cros_cbi_get_fw_config, enum cbi_fw_config_field_id,
		uint32_t *);
FAKE_VOID_FUNC(bmi3xx_interrupt, enum gpio_signal);
FAKE_VOID_FUNC(lsm6dsm_interrupt, enum gpio_signal);
FAKE_VOID_FUNC(icm42607_interrupt, enum gpio_signal);
FAKE_VOID_FUNC(bma4xx_interrupt, enum gpio_signal);
FAKE_VOID_FUNC(lis2dw12_interrupt, enum gpio_signal);

static void test_before(void *fixture)
{
	RESET_FAKE(cros_cbi_get_fw_config);
	RESET_FAKE(cbi_get_ssfc);
	RESET_FAKE(bmi3xx_interrupt);
	RESET_FAKE(lsm6dsm_interrupt);
	RESET_FAKE(icm42607_interrupt);
	RESET_FAKE(bma4xx_interrupt);
	RESET_FAKE(lis2dw12_interrupt);
}

ZTEST_SUITE(teliks, NULL, NULL, test_before, NULL, NULL);

static bool clamshell_mode;

static int cbi_get_form_factor_config(enum cbi_fw_config_field_id field,
				      uint32_t *value)
{
	if (field == FORM_FACTOR)
		*value = clamshell_mode ? CLAMSHELL : CONVERTIBLE;
	return 0;
}

static int cbi_get_form_factor_config_error(enum cbi_fw_config_field_id field,
					    uint32_t *value)
{
	return -1;
}

ZTEST(teliks, test_board_setup_init_clamshell)
{
	const struct device *tablet_mode_gpio = DEVICE_DT_GET(
		DT_GPIO_CTLR(DT_NODELABEL(gpio_tablet_mode_l), gpios));
	const gpio_port_pins_t tablet_mode_pin =
		DT_GPIO_PIN(DT_NODELABEL(gpio_tablet_mode_l), gpios);
	const struct device *base_imu_gpio = DEVICE_DT_GET(
		DT_GPIO_CTLR(DT_NODELABEL(gpio_imu_int_l), gpios));
	const gpio_port_pins_t base_imu_pin =
		DT_GPIO_PIN(DT_NODELABEL(gpio_imu_int_l), gpios);
	const struct device *lid_accel_gpio = DEVICE_DT_GET(
		DT_GPIO_CTLR(DT_NODELABEL(gpio_acc_int_l), gpios));
	const gpio_port_pins_t lid_accel_pin =
		DT_GPIO_PIN(DT_NODELABEL(gpio_acc_int_l), gpios);
	int interrupt_count;

	/* CBI config error */
	cros_cbi_get_fw_config_fake.custom_fake =
		cbi_get_form_factor_config_error;
	board_setup_init();
	alt_sensor_init();

	/* reset tablet mode for initialize status.
	 * enable int_imu and int_tablet_mode before clashell_init
	 * for the priorities of sensor_enable_irqs and
	 * gmr_tablet_switch_init is earlier.
	 */
	tablet_reset();
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_tablet_mode));
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_imu));
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_lid_imu));

	cros_cbi_get_fw_config_fake.custom_fake = cbi_get_form_factor_config;

	clamshell_mode = true;
	board_setup_init();
	/* Clamshel mode */
	alt_sensor_init();

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

	/* Clear base and lid sensor interrupt call count before test */
	bmi3xx_interrupt_fake.call_count = 0;
	lsm6dsm_interrupt_fake.call_count = 0;
	icm42607_interrupt_fake.call_count = 0;
	bma4xx_interrupt_fake.call_count = 0;
	lis2dw12_interrupt_fake.call_count = 0;

	/* Verify base and lid sensor interrupt is disabled. */
	zassert_ok(gpio_emul_input_set(base_imu_gpio, base_imu_pin, 1), NULL);
	k_sleep(K_MSEC(100));
	zassert_ok(gpio_emul_input_set(base_imu_gpio, base_imu_pin, 0), NULL);
	k_sleep(K_MSEC(100));
	zassert_ok(gpio_emul_input_set(lid_accel_gpio, lid_accel_pin, 1), NULL);
	k_sleep(K_MSEC(100));
	zassert_ok(gpio_emul_input_set(lid_accel_gpio, lid_accel_pin, 0), NULL);
	k_sleep(K_MSEC(100));

	interrupt_count = bmi3xx_interrupt_fake.call_count +
			  lsm6dsm_interrupt_fake.call_count +
			  icm42607_interrupt_fake.call_count +
			  bma4xx_interrupt_fake.call_count +
			  lis2dw12_interrupt_fake.call_count;
	zassert_equal(interrupt_count, 0);
}

static int ssfc_data;

static int cbi_get_ssfc_mock(uint32_t *ssfc)
{
	*ssfc = ssfc_data;
	return 0;
}

ZTEST(teliks, test_board_setup_init_convertible)
{
	const struct device *tablet_mode_gpio = DEVICE_DT_GET(
		DT_GPIO_CTLR(DT_NODELABEL(gpio_tablet_mode_l), gpios));
	const gpio_port_pins_t tablet_mode_pin =
		DT_GPIO_PIN(DT_NODELABEL(gpio_tablet_mode_l), gpios);
	const struct device *base_imu_gpio = DEVICE_DT_GET(
		DT_GPIO_CTLR(DT_NODELABEL(gpio_imu_int_l), gpios));
	const gpio_port_pins_t base_imu_pin =
		DT_GPIO_PIN(DT_NODELABEL(gpio_imu_int_l), gpios);
	const struct device *lid_accel_gpio = DEVICE_DT_GET(
		DT_GPIO_CTLR(DT_NODELABEL(gpio_acc_int_l), gpios));
	const gpio_port_pins_t lid_accel_pin =
		DT_GPIO_PIN(DT_NODELABEL(gpio_acc_int_l), gpios);
	int interrupt_count;

	/* Initial ssfc data for BMA422 and BMI323. */
	cbi_get_ssfc_fake.custom_fake = cbi_get_ssfc_mock;
	ssfc_data = 0x9;
	cros_cbi_ssfc_init();

	/* reset tablet mode for initialize status.
	 * enable int_imu and int_tablet_mode before clashell_init
	 * for the priorities of sensor_enable_irqs and
	 * gmr_tablet_switch_init is earlier.
	 */
	tablet_reset();
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_tablet_mode));
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_imu));
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_lid_imu));

	alt_sensor_init();

	cros_cbi_get_fw_config_fake.custom_fake = cbi_get_form_factor_config;
	clamshell_mode = false;
	board_setup_init();

	/* Verify gmr_tablet_switch is disabled, by checking the side effects
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

	/* Clear base and lid sensor interrupt call count before test */
	bmi3xx_interrupt_fake.call_count = 0;
	lsm6dsm_interrupt_fake.call_count = 0;
	icm42607_interrupt_fake.call_count = 0;
	bma4xx_interrupt_fake.call_count = 0;
	lis2dw12_interrupt_fake.call_count = 0;

	/* Verify base and lid sensor interrupt is disabled. */
	zassert_ok(gpio_emul_input_set(base_imu_gpio, base_imu_pin, 1), NULL);
	k_sleep(K_MSEC(100));
	zassert_ok(gpio_emul_input_set(base_imu_gpio, base_imu_pin, 0), NULL);
	k_sleep(K_MSEC(100));
	zassert_ok(gpio_emul_input_set(lid_accel_gpio, lid_accel_pin, 1), NULL);
	k_sleep(K_MSEC(100));
	zassert_ok(gpio_emul_input_set(lid_accel_gpio, lid_accel_pin, 0), NULL);
	k_sleep(K_MSEC(100));

	interrupt_count = bmi3xx_interrupt_fake.call_count +
			  lsm6dsm_interrupt_fake.call_count +
			  icm42607_interrupt_fake.call_count +
			  bma4xx_interrupt_fake.call_count +
			  lis2dw12_interrupt_fake.call_count;
	zassert_equal(interrupt_count, 2);

	zassert_equal(bmi3xx_interrupt_fake.call_count, 1);
	zassert_equal(lsm6dsm_interrupt_fake.call_count, 0);
	zassert_equal(icm42607_interrupt_fake.call_count, 0);
	zassert_equal(bma4xx_interrupt_fake.call_count, 1);
	zassert_equal(lis2dw12_interrupt_fake.call_count, 0);
}

ZTEST(teliks, test_alt_sensor)
{
	const struct device *base_imu_gpio = DEVICE_DT_GET(
		DT_GPIO_CTLR(DT_NODELABEL(gpio_imu_int_l), gpios));
	const gpio_port_pins_t base_imu_pin =
		DT_GPIO_PIN(DT_NODELABEL(gpio_imu_int_l), gpios);
	const struct device *lid_accel_gpio = DEVICE_DT_GET(
		DT_GPIO_CTLR(DT_NODELABEL(gpio_acc_int_l), gpios));
	const gpio_port_pins_t lid_accel_pin =
		DT_GPIO_PIN(DT_NODELABEL(gpio_acc_int_l), gpios);

	/* Initial ssfc data for LSM6DSM and LIS2DW. */
	cbi_get_ssfc_fake.custom_fake = cbi_get_ssfc_mock;
	ssfc_data = 0x12;
	cros_cbi_ssfc_init();

	/* Enable the interrupt int_imu and int_lid_imu */
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_imu));
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_lid_imu));

	alt_sensor_init();

	/* Clear base and lid sensor interrupt call count before test */
	bmi3xx_interrupt_fake.call_count = 0;
	lsm6dsm_interrupt_fake.call_count = 0;
	icm42607_interrupt_fake.call_count = 0;
	bma4xx_interrupt_fake.call_count = 0;
	lis2dw12_interrupt_fake.call_count = 0;

	zassert_ok(gpio_emul_input_set(base_imu_gpio, base_imu_pin, 1), NULL);
	k_sleep(K_MSEC(100));
	zassert_ok(gpio_emul_input_set(base_imu_gpio, base_imu_pin, 0), NULL);
	k_sleep(K_MSEC(100));
	zassert_ok(gpio_emul_input_set(lid_accel_gpio, lid_accel_pin, 1), NULL);
	k_sleep(K_MSEC(100));
	zassert_ok(gpio_emul_input_set(lid_accel_gpio, lid_accel_pin, 0), NULL);
	k_sleep(K_MSEC(100));

	zassert_equal(bmi3xx_interrupt_fake.call_count, 0);
	zassert_equal(lsm6dsm_interrupt_fake.call_count, 1);
	zassert_equal(icm42607_interrupt_fake.call_count, 0);
	zassert_equal(bma4xx_interrupt_fake.call_count, 0);
	zassert_equal(lis2dw12_interrupt_fake.call_count, 1);
}

ZTEST(teliks, test_alt_sensor_icm42607)
{
	const struct device *base_imu_gpio = DEVICE_DT_GET(
		DT_GPIO_CTLR(DT_NODELABEL(gpio_imu_int_l), gpios));
	const gpio_port_pins_t base_imu_pin =
		DT_GPIO_PIN(DT_NODELABEL(gpio_imu_int_l), gpios);
	const struct device *lid_accel_gpio = DEVICE_DT_GET(
		DT_GPIO_CTLR(DT_NODELABEL(gpio_acc_int_l), gpios));
	const gpio_port_pins_t lid_accel_pin =
		DT_GPIO_PIN(DT_NODELABEL(gpio_acc_int_l), gpios);

	/* Initial ssfc data for ICM42607 and LIS2DW. */
	cbi_get_ssfc_fake.custom_fake = cbi_get_ssfc_mock;
	ssfc_data = 0x13;
	cros_cbi_ssfc_init();

	/* Enable the interrupt int_imu and int_lid_imu */
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_imu));
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_lid_imu));

	alt_sensor_init();

	/* Clear base and lid sensor interrupt call count before test */
	bmi3xx_interrupt_fake.call_count = 0;
	lsm6dsm_interrupt_fake.call_count = 0;
	icm42607_interrupt_fake.call_count = 0;
	bma4xx_interrupt_fake.call_count = 0;
	lis2dw12_interrupt_fake.call_count = 0;

	zassert_ok(gpio_emul_input_set(base_imu_gpio, base_imu_pin, 1), NULL);
	k_sleep(K_MSEC(100));
	zassert_ok(gpio_emul_input_set(base_imu_gpio, base_imu_pin, 0), NULL);
	k_sleep(K_MSEC(100));
	zassert_ok(gpio_emul_input_set(lid_accel_gpio, lid_accel_pin, 1), NULL);
	k_sleep(K_MSEC(100));
	zassert_ok(gpio_emul_input_set(lid_accel_gpio, lid_accel_pin, 0), NULL);
	k_sleep(K_MSEC(100));

	zassert_equal(bmi3xx_interrupt_fake.call_count, 0);
	zassert_equal(lsm6dsm_interrupt_fake.call_count, 0);
	zassert_equal(icm42607_interrupt_fake.call_count, 1);
	zassert_equal(bma4xx_interrupt_fake.call_count, 0);
	zassert_equal(lis2dw12_interrupt_fake.call_count, 1);
}

ZTEST(teliks, test_battery_hw_present)
{
	const struct device *batt_pres_gpio = DEVICE_DT_GET(
		DT_GPIO_CTLR(DT_NODELABEL(gpio_ec_battery_pres_odl), gpios));
	const gpio_port_pins_t batt_pres_pin =
		DT_GPIO_PIN(DT_NODELABEL(gpio_ec_battery_pres_odl), gpios);

	zassert_not_null(batt_pres_gpio, NULL);

	zassert_ok(gpio_emul_input_set(batt_pres_gpio, batt_pres_pin, 0));
	zassert_equal(BP_YES, battery_hw_present());

	zassert_ok(gpio_emul_input_set(batt_pres_gpio, batt_pres_pin, 1));
	zassert_equal(BP_NO, battery_hw_present());
}

ZTEST(teliks, test_get_scancode_set2)
{
	/* Test some special keys of the customization matrix */
	zassert_equal(get_scancode_set2(6, 13), SCANCODE_LEFT_ALT);
	zassert_equal(get_scancode_set2(1, 14), SCANCODE_LEFT_CTRL);

	/* Test out of the matrix range */
	zassert_equal(get_scancode_set2(8, 12), 0);
	zassert_equal(get_scancode_set2(0, 18), 0);
}

ZTEST(teliks, test_set_scancode_set2)
{
	/* Set some special keys and read back */
	zassert_equal(get_scancode_set2(1, 0), 0);
	set_scancode_set2(1, 0, SCANCODE_LEFT_WIN);
	zassert_equal(get_scancode_set2(1, 0), SCANCODE_LEFT_WIN);

	zassert_equal(get_scancode_set2(4, 0), 0);
	set_scancode_set2(4, 0, SCANCODE_CAPSLOCK);
	zassert_equal(get_scancode_set2(4, 0), SCANCODE_CAPSLOCK);

	zassert_equal(get_scancode_set2(0, 13), 0);
	set_scancode_set2(0, 13, SCANCODE_F15);
	zassert_equal(get_scancode_set2(0, 13), SCANCODE_F15);
}

ZTEST(teliks, test_get_keycap_label)
{
	zassert_equal(get_keycap_label(3, 0), KLLI_SEARC);
	zassert_equal(get_keycap_label(0, 4), KLLI_F10);
	zassert_equal(get_keycap_label(8, 12), KLLI_UNKNO);
	zassert_equal(get_keycap_label(0, 18), KLLI_UNKNO);
}

ZTEST(teliks, test_set_keycap_label)
{
	zassert_equal(get_keycap_label(2, 0), KLLI_UNKNO);
	set_keycap_label(2, 0, KLLI_SEARC);
	zassert_equal(get_keycap_label(2, 0), KLLI_SEARC);

	zassert_equal(get_keycap_label(0, 14), KLLI_UNKNO);
	set_keycap_label(0, 14, KLLI_F15);
	zassert_equal(get_keycap_label(0, 14), KLLI_F15);
}
