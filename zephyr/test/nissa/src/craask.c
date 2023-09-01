/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "button.h"
#include "craask.h"
#include "cros_board_info.h"
#include "cros_cbi.h"
#include "gpio/gpio_int.h"
#include "hooks.h"
#include "keyboard_8042_sharedlib.h"
#include "keyboard_raw.h"
#include "keyboard_scan.h"
#include "motionsense_sensors.h"
#include "nissa_sub_board.h"
#include "tablet_mode.h"

#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/fff.h>
#include <zephyr/logging/log.h>
#include <zephyr/ztest.h>

LOG_MODULE_REGISTER(nissa, LOG_LEVEL_INF);

FAKE_VALUE_FUNC(int, cros_cbi_get_fw_config, enum cbi_fw_config_field_id,
		uint32_t *);
FAKE_VALUE_FUNC(int, cbi_get_board_version, uint32_t *);
FAKE_VALUE_FUNC(int, cbi_get_ssfc, uint32_t *);
FAKE_VALUE_FUNC(enum nissa_sub_board_type, nissa_get_sb_type);
FAKE_VOID_FUNC(usb_interrupt_c1, enum gpio_signal);
FAKE_VOID_FUNC(bmi3xx_interrupt, enum gpio_signal);
FAKE_VOID_FUNC(lsm6dso_interrupt, enum gpio_signal);
FAKE_VOID_FUNC(bma4xx_interrupt, enum gpio_signal);
FAKE_VOID_FUNC(lis2dw12_interrupt, enum gpio_signal);
FAKE_VOID_FUNC(fan_set_count, int);

FAKE_VOID_FUNC(lpc_keyboard_resume_irq);

static void test_before(void *fixture)
{
	RESET_FAKE(cbi_get_board_version);
	RESET_FAKE(cros_cbi_get_fw_config);
	RESET_FAKE(cbi_get_ssfc);
	RESET_FAKE(nissa_get_sb_type);
	RESET_FAKE(bmi3xx_interrupt);
	RESET_FAKE(lsm6dso_interrupt);
	RESET_FAKE(bma4xx_interrupt);
	RESET_FAKE(lis2dw12_interrupt);
	RESET_FAKE(fan_set_count);
}

ZTEST_SUITE(craask, NULL, NULL, test_before, NULL, NULL);

static int board_version;

static int cbi_get_board_version_mock(uint32_t *value)
{
	*value = board_version;
	return 0;
}

int clock_get_freq(void)
{
	return 16000000;
}

ZTEST(craask, test_volum_up_dn_buttons)
{
	cbi_get_board_version_fake.custom_fake = cbi_get_board_version_mock;

	nissa_get_sb_type_fake.return_val = NISSA_SB_C_A;

	board_version = 1;
	buttons_init();
	zassert_equal(buttons[BUTTON_VOLUME_UP].gpio, GPIO_VOLUME_UP_L);
	zassert_equal(buttons[BUTTON_VOLUME_DOWN].gpio, GPIO_VOLUME_DOWN_L);

	board_version = 2;
	buttons_init();
	zassert_equal(buttons[BUTTON_VOLUME_UP].gpio, GPIO_VOLUME_UP_L);
	zassert_equal(buttons[BUTTON_VOLUME_DOWN].gpio, GPIO_VOLUME_DOWN_L);

	board_version = 3;
	buttons_init();
	zassert_equal(buttons[BUTTON_VOLUME_UP].gpio, GPIO_VOLUME_DOWN_L);
	zassert_equal(buttons[BUTTON_VOLUME_DOWN].gpio, GPIO_VOLUME_UP_L);
}

static bool has_keypad;

static int cbi_get_keyboard_configuration(enum cbi_fw_config_field_id field,
					  uint32_t *value)
{
	if (field != FW_KB_NUMERIC_PAD)
		return -EINVAL;

	*value = has_keypad ? FW_KB_NUMERIC_PAD_PRESENT :
			      FW_KB_NUMERIC_PAD_ABSENT;
	return 0;
}

ZTEST(craask, test_keyboard_configuration)
{
	cros_cbi_get_fw_config_fake.custom_fake =
		cbi_get_keyboard_configuration;

	has_keypad = false;
	kb_init();
	zassert_equal(keyboard_raw_get_cols(), KEYBOARD_COLS_NO_KEYPAD);
	zassert_equal(keyscan_config.actual_key_mask[11], 0xfa);
	zassert_equal(keyscan_config.actual_key_mask[12], 0xca);
	zassert_equal(keyscan_config.actual_key_mask[13], 0x00);
	zassert_equal(keyscan_config.actual_key_mask[14], 0x00);
	zassert_equal_ptr(board_vivaldi_keybd_config(), &craask_kb);

	/* Initialize keyboard_cols for next test */
	keyboard_raw_set_cols(KEYBOARD_COLS_MAX);

	has_keypad = true;
	kb_init();
	zassert_equal(keyboard_raw_get_cols(), KEYBOARD_COLS_WITH_KEYPAD);
	zassert_equal(keyscan_config.actual_key_mask[11], 0xfe);
	zassert_equal(keyscan_config.actual_key_mask[12], 0xff);
	zassert_equal(keyscan_config.actual_key_mask[13], 0xff);
	zassert_equal(keyscan_config.actual_key_mask[14], 0xff);
	zassert_equal_ptr(board_vivaldi_keybd_config(), &craask_kb_w_kb_numpad);
}

static bool keyboard_ca_fr;

static int cbi_get_keyboard_type_config(enum cbi_fw_config_field_id field,
					uint32_t *value)
{
	if (field != FW_KB_TYPE)
		return -EINVAL;

	*value = keyboard_ca_fr ? FW_KB_TYPE_CA_FR : FW_KB_TYPE_DEFAULT;
	return 0;
}

ZTEST(craask, test_keyboard_type)
{
	uint16_t forwardslash_pipe_key = get_scancode_set2(2, 7);
	uint16_t right_control_key = get_scancode_set2(4, 0);

	cros_cbi_get_fw_config_fake.custom_fake = cbi_get_keyboard_type_config;

	keyboard_ca_fr = false;
	kb_init();
	zassert_equal(get_scancode_set2(4, 0), right_control_key);
	zassert_equal(get_scancode_set2(2, 7), forwardslash_pipe_key);

	keyboard_ca_fr = true;
	kb_init();
	zassert_equal(get_scancode_set2(4, 0), forwardslash_pipe_key);
	zassert_equal(get_scancode_set2(2, 7), right_control_key);
}

static bool lid_inverted;

static int cbi_get_lid_orientation_config(enum cbi_fw_config_field_id field,
					  uint32_t *value)
{
	if (field == FW_LID_INVERSION)
		*value = lid_inverted ? FW_LID_XY_ROT_180 : FW_LID_REGULAR;

	return 0;
}

ZTEST(craask, test_base_orientation)
{
	const int BASE_SENSOR = SENSOR_ID(DT_NODELABEL(base_accel));
	const void *const normal_rotation =
		&SENSOR_ROT_STD_REF_NAME(DT_NODELABEL(base_rot_ref));
	const void *const inverted_rotation =
		&SENSOR_ROT_STD_REF_NAME(DT_NODELABEL(base_rot_ver1));

	motion_sensors[BASE_SENSOR].rot_standard_ref = normal_rotation;

	cbi_get_board_version_fake.custom_fake = cbi_get_board_version_mock;
	board_version = 2;
	form_factor_init();
	zassert_equal_ptr(motion_sensors[BASE_SENSOR].rot_standard_ref,
			  normal_rotation,
			  "base normal orientation should be base_rot_ref");

	RESET_FAKE(cbi_get_board_version);
	cbi_get_board_version_fake.return_val = EINVAL;
	form_factor_init();
	zassert_equal_ptr(motion_sensors[BASE_SENSOR].rot_standard_ref,
			  normal_rotation,
			  "errors should leave the rotation unchanged");

	cbi_get_board_version_fake.custom_fake = cbi_get_board_version_mock;
	board_version = 1;
	form_factor_init();
	zassert_equal_ptr(motion_sensors[BASE_SENSOR].rot_standard_ref,
			  inverted_rotation,
			  "base inverted orientation should be base_rot_ver1");
}

ZTEST(craask, test_lid_orientation)
{
	const int LID_SENSOR = SENSOR_ID(DT_NODELABEL(lid_accel));
	const void *const normal_rotation =
		&SENSOR_ROT_STD_REF_NAME(DT_NODELABEL(lid_rot_ref));
	const void *const inverted_rotation =
		&SENSOR_ROT_STD_REF_NAME(DT_NODELABEL(lid_rot_bma422));

	motion_sensors[LID_SENSOR].rot_standard_ref = normal_rotation;

	cros_cbi_get_fw_config_fake.custom_fake =
		cbi_get_lid_orientation_config;

	lid_inverted = false;
	form_factor_init();
	zassert_equal_ptr(motion_sensors[LID_SENSOR].rot_standard_ref,
			  normal_rotation,
			  "normal orientation should be lid_rot_ref");

	RESET_FAKE(cros_cbi_get_fw_config);
	cros_cbi_get_fw_config_fake.return_val = EINVAL;
	form_factor_init();
	zassert_equal_ptr(motion_sensors[LID_SENSOR].rot_standard_ref,
			  normal_rotation,
			  "errors should leave the rotation unchanged");

	cros_cbi_get_fw_config_fake.custom_fake =
		cbi_get_lid_orientation_config;

	lid_inverted = true;
	form_factor_init();
	zassert_equal_ptr(
		motion_sensors[LID_SENSOR].rot_standard_ref, inverted_rotation,
		"inverted orientation should be same as lid_rot_bma422");
}

static bool clamshell_mode;

static int cbi_get_form_factor_config(enum cbi_fw_config_field_id field,
				      uint32_t *value)
{
	if (field == FORM_FACTOR)
		*value = clamshell_mode ? CLAMSHELL : CONVERTIBLE;

	return 0;
}

ZTEST(craask, test_convertible)
{
	const struct device *tablet_mode_gpio = DEVICE_DT_GET(
		DT_GPIO_CTLR(DT_NODELABEL(gpio_tablet_mode_l), gpios));
	const gpio_port_pins_t tablet_mode_pin =
		DT_GPIO_PIN(DT_NODELABEL(gpio_tablet_mode_l), gpios);
	const struct device *base_imu_gpio = DEVICE_DT_GET(
		DT_GPIO_CTLR(DT_NODELABEL(gpio_imu_int_l), gpios));
	const gpio_port_pins_t base_imu_pin =
		DT_GPIO_PIN(DT_NODELABEL(gpio_imu_int_l), gpios);
	int interrupt_count;

	/* reset tablet mode for initialize status.
	 * enable int_imu and int_tablet_mode before clashell_init
	 * for the priorities of sensor_enable_irqs and
	 * gmr_tablet_switch_init is earlier.
	 */
	tablet_reset();
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_tablet_mode));
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_imu));

	cros_cbi_get_fw_config_fake.custom_fake = cbi_get_form_factor_config;

	clamshell_mode = false;
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

	/* Clear base_imu_irq call count before test */
	bmi3xx_interrupt_fake.call_count = 0;
	lsm6dso_interrupt_fake.call_count = 0;

	/* Verify base_imu_irq is enabled. Interrupt is configured
	 * GPIO_INT_EDGE_FALLING, so set high, then set low.
	 */
	zassert_ok(gpio_emul_input_set(base_imu_gpio, base_imu_pin, 1), NULL);
	k_sleep(K_MSEC(100));
	zassert_ok(gpio_emul_input_set(base_imu_gpio, base_imu_pin, 0), NULL);
	k_sleep(K_MSEC(100));
	interrupt_count = bmi3xx_interrupt_fake.call_count +
			  lsm6dso_interrupt_fake.call_count;
	zassert_equal(interrupt_count, 1);
}

ZTEST(craask, test_clamshell)
{
	const struct device *tablet_mode_gpio = DEVICE_DT_GET(
		DT_GPIO_CTLR(DT_NODELABEL(gpio_tablet_mode_l), gpios));
	const gpio_port_pins_t tablet_mode_pin =
		DT_GPIO_PIN(DT_NODELABEL(gpio_tablet_mode_l), gpios);
	const struct device *base_imu_gpio = DEVICE_DT_GET(
		DT_GPIO_CTLR(DT_NODELABEL(gpio_imu_int_l), gpios));
	const gpio_port_pins_t base_imu_pin =
		DT_GPIO_PIN(DT_NODELABEL(gpio_imu_int_l), gpios);
	int interrupt_count;

	/* reset tablet mode for initialize status.
	 * enable int_imu and int_tablet_mode before clashell_init
	 * for the priorities of sensor_enable_irqs and
	 * gmr_tablet_switch_init is earlier.
	 */
	tablet_reset();
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_tablet_mode));
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_imu));

	cros_cbi_get_fw_config_fake.custom_fake = cbi_get_form_factor_config;

	clamshell_mode = true;
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

	/* Clear base_imu_irq call count before test */
	bmi3xx_interrupt_fake.call_count = 0;
	lsm6dso_interrupt_fake.call_count = 0;

	/* Verify base_imu_irq is disabled. */
	zassert_ok(gpio_emul_input_set(base_imu_gpio, base_imu_pin, 1), NULL);
	k_sleep(K_MSEC(100));
	zassert_ok(gpio_emul_input_set(base_imu_gpio, base_imu_pin, 0), NULL);
	k_sleep(K_MSEC(100));
	interrupt_count = bmi3xx_interrupt_fake.call_count +
			  lsm6dso_interrupt_fake.call_count;
	zassert_equal(interrupt_count, 0);
}

static int ssfc_data;

static int cbi_get_ssfc_mock(uint32_t *ssfc)
{
	*ssfc = ssfc_data;
	return 0;
}

ZTEST(craask, test_alt_sensor_base_lsm6dso)
{
	const struct device *base_imu_gpio = DEVICE_DT_GET(
		DT_GPIO_CTLR(DT_NODELABEL(gpio_imu_int_l), gpios));
	const gpio_port_pins_t base_imu_pin =
		DT_GPIO_PIN(DT_NODELABEL(gpio_imu_int_l), gpios);

	/* Initial ssfc data for LSM6DSO base sensor. */
	cbi_get_ssfc_fake.custom_fake = cbi_get_ssfc_mock;
	ssfc_data = 0x00;
	cros_cbi_ssfc_init();

	/* sensor_enable_irqs enable the interrupt int_imu */
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_imu));

	alt_sensor_init();

	/* Clear base_imu_irq call count before test */
	bmi3xx_interrupt_fake.call_count = 0;
	lsm6dso_interrupt_fake.call_count = 0;

	zassert_ok(gpio_emul_input_set(base_imu_gpio, base_imu_pin, 1), NULL);
	k_sleep(K_MSEC(100));
	zassert_ok(gpio_emul_input_set(base_imu_gpio, base_imu_pin, 0), NULL);
	k_sleep(K_MSEC(100));

	zassert_equal(bmi3xx_interrupt_fake.call_count, 0);
	zassert_equal(lsm6dso_interrupt_fake.call_count, 1);
}

ZTEST(craask, test_alt_sensor_base_bmi323)
{
	const struct device *base_imu_gpio = DEVICE_DT_GET(
		DT_GPIO_CTLR(DT_NODELABEL(gpio_imu_int_l), gpios));
	const gpio_port_pins_t base_imu_pin =
		DT_GPIO_PIN(DT_NODELABEL(gpio_imu_int_l), gpios);

	/* Initial ssfc data for BMI323 base sensor. */
	cbi_get_ssfc_fake.custom_fake = cbi_get_ssfc_mock;
	ssfc_data = 0x10;
	cros_cbi_ssfc_init();

	/* sensor_enable_irqs enable the interrupt int_imu */
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_imu));

	alt_sensor_init();

	/* Clear base_imu_irq call count before test */
	bmi3xx_interrupt_fake.call_count = 0;
	lsm6dso_interrupt_fake.call_count = 0;

	zassert_ok(gpio_emul_input_set(base_imu_gpio, base_imu_pin, 1), NULL);
	k_sleep(K_MSEC(100));
	zassert_ok(gpio_emul_input_set(base_imu_gpio, base_imu_pin, 0), NULL);
	k_sleep(K_MSEC(100));

	zassert_equal(bmi3xx_interrupt_fake.call_count, 1);
	zassert_equal(lsm6dso_interrupt_fake.call_count, 0);
}

ZTEST(craask, test_alt_sensor_lid_lis2dw12)
{
	const struct device *lid_accel_gpio = DEVICE_DT_GET(
		DT_GPIO_CTLR(DT_NODELABEL(gpio_acc_int_l), gpios));
	const gpio_port_pins_t lid_accel_pin =
		DT_GPIO_PIN(DT_NODELABEL(gpio_acc_int_l), gpios);

	/* Initial ssfc data for LIS2DW12 lid sensor. */
	cbi_get_ssfc_fake.custom_fake = cbi_get_ssfc_mock;
	ssfc_data = 0x00;
	cros_cbi_ssfc_init();

	/* sensor_enable_irqs enable the interrupt int_lid_accel */
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_lid_accel));

	alt_sensor_init();

	/* Clear base_imu_irq call count before test */
	lis2dw12_interrupt_fake.call_count = 0;
	bma4xx_interrupt_fake.call_count = 0;

	zassert_ok(gpio_emul_input_set(lid_accel_gpio, lid_accel_pin, 1), NULL);
	k_sleep(K_MSEC(100));
	zassert_ok(gpio_emul_input_set(lid_accel_gpio, lid_accel_pin, 0), NULL);
	k_sleep(K_MSEC(100));

	zassert_equal(lis2dw12_interrupt_fake.call_count, 1);
	zassert_equal(bma4xx_interrupt_fake.call_count, 0);
}

ZTEST(craask, test_alt_sensor_lid_bma422)
{
	const struct device *lid_accel_gpio = DEVICE_DT_GET(
		DT_GPIO_CTLR(DT_NODELABEL(gpio_acc_int_l), gpios));
	const gpio_port_pins_t lid_accel_pin =
		DT_GPIO_PIN(DT_NODELABEL(gpio_acc_int_l), gpios);

	/* Initial ssfc data for BMA422 lid sensor. */
	cbi_get_ssfc_fake.custom_fake = cbi_get_ssfc_mock;
	ssfc_data = 0x04;
	cros_cbi_ssfc_init();

	/* sensor_enable_irqs enable the interrupt int_lid_accel */
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_lid_accel));

	alt_sensor_init();

	/* Clear base_imu_irq call count before test */
	lis2dw12_interrupt_fake.call_count = 0;
	bma4xx_interrupt_fake.call_count = 0;

	zassert_ok(gpio_emul_input_set(lid_accel_gpio, lid_accel_pin, 1), NULL);
	k_sleep(K_MSEC(100));
	zassert_ok(gpio_emul_input_set(lid_accel_gpio, lid_accel_pin, 0), NULL);
	k_sleep(K_MSEC(100));

	zassert_equal(lis2dw12_interrupt_fake.call_count, 0);
	zassert_equal(bma4xx_interrupt_fake.call_count, 1);
}

static bool fan_present;

static int cbi_get_fan_fw_config(enum cbi_fw_config_field_id field,
				 uint32_t *value)
{
	if (field != FW_FAN)
		return -EINVAL;

	*value = fan_present ? FW_FAN_PRESENT : FW_FAN_NOT_PRESENT;
	return 0;
}

ZTEST(craask, test_fan_present)
{
	int flags;

	cros_cbi_get_fw_config_fake.custom_fake = cbi_get_fan_fw_config;

	fan_present = true;
	fan_init();

	zassert_equal(fan_set_count_fake.call_count, 0);
	zassert_ok(gpio_pin_get_config_dt(
		GPIO_DT_FROM_NODELABEL(gpio_fan_enable), &flags));
	zassert_equal(flags, GPIO_OUTPUT | GPIO_OUTPUT_INIT_LOW,
		      "actual GPIO flags were %#x", flags);
}

ZTEST(craask, test_fan_absent)
{
	int flags;

	cros_cbi_get_fw_config_fake.custom_fake = cbi_get_fan_fw_config;

	fan_present = false;
	fan_init();

	zassert_equal(fan_set_count_fake.call_count, 1,
		      "function actually called %d times",
		      fan_set_count_fake.call_count);
	zassert_equal(fan_set_count_fake.arg0_val, 0, "parameter value was %d",
		      fan_set_count_fake.arg0_val);

	/* Fan enable is left unconfigured */
	zassert_ok(gpio_pin_get_config_dt(
		GPIO_DT_FROM_NODELABEL(gpio_fan_enable), &flags));
	zassert_equal(flags, 0, "actual GPIO flags were %#x", flags);
}
