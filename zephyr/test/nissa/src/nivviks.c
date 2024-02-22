/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "battery.h"
#include "cros_cbi.h"
#include "hooks.h"
#include "keyboard_protocol.h"
#include "led_onoff_states.h"
#include "led_pwm.h"
#include "motionsense_sensors.h"
#include "pwm_mock.h"

#include <zephyr/fff.h>
#include <zephyr/logging/log.h>
#include <zephyr/ztest.h>

LOG_MODULE_REGISTER(nissa, LOG_LEVEL_INF);

void fan_init(void);
void form_factor_init(void);

FAKE_VALUE_FUNC(int, cros_cbi_get_fw_config, enum cbi_fw_config_field_id,
		uint32_t *);
FAKE_VALUE_FUNC(enum battery_present, battery_is_present);
FAKE_VOID_FUNC(board_set_active_charge_port, int);
FAKE_VOID_FUNC(fan_set_count, int);
FAKE_VOID_FUNC(set_pwm_led_color, enum pwm_led_id, int);

static void nivviks_test_before(void *fixture)
{
	RESET_FAKE(cros_cbi_get_fw_config);
	RESET_FAKE(battery_is_present);
	RESET_FAKE(board_set_active_charge_port);
	RESET_FAKE(fan_set_count);
	RESET_FAKE(set_pwm_led_color);
}

ZTEST_SUITE(nivviks, NULL, NULL, nivviks_test_before, NULL, NULL);

static int get_fan_config_present(enum cbi_fw_config_field_id field,
				  uint32_t *value)
{
	zassert_equal(field, FW_FAN);
	*value = FW_FAN_PRESENT;
	return 0;
}

static int get_fan_config_absent(enum cbi_fw_config_field_id field,
				 uint32_t *value)
{
	zassert_equal(field, FW_FAN);
	*value = FW_FAN_NOT_PRESENT;
	return 0;
}

ZTEST(nivviks, test_fan_present)
{
	int flags;

	cros_cbi_get_fw_config_fake.custom_fake = get_fan_config_present;
	fan_init();

	zassert_equal(fan_set_count_fake.call_count, 0);
	zassert_ok(gpio_pin_get_config_dt(
		GPIO_DT_FROM_NODELABEL(gpio_fan_enable), &flags));
	zassert_equal(flags, GPIO_OUTPUT | GPIO_OUTPUT_INIT_LOW,
		      "actual GPIO flags were %#x", flags);
}

ZTEST(nivviks, test_fan_absent)
{
	int flags;

	cros_cbi_get_fw_config_fake.custom_fake = get_fan_config_absent;
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

ZTEST(nivviks, test_fan_cbi_error)
{
	int flags;

	cros_cbi_get_fw_config_fake.return_val = EINVAL;
	fan_init();

	zassert_equal(fan_set_count_fake.call_count, 0);
	zassert_ok(gpio_pin_get_config_dt(
		GPIO_DT_FROM_NODELABEL(gpio_fan_enable), &flags));
	zassert_equal(flags, 0, "actual GPIO flags were %#x", flags);
}

static int get_base_orientation_normal(enum cbi_fw_config_field_id field,
				       uint32_t *value)
{
	zassert_equal(field, FW_BASE_INVERSION);
	*value = FW_BASE_REGULAR;
	return 0;
}

static int get_base_orientation_inverted(enum cbi_fw_config_field_id field,
					 uint32_t *value)
{
	zassert_equal(field, FW_BASE_INVERSION);
	*value = FW_BASE_INVERTED;
	return 0;
}

ZTEST(nivviks, test_base_inversion)
{
	const int BASE_ACCEL = SENSOR_ID(DT_NODELABEL(base_accel));
	const void *const normal_rotation =
		&SENSOR_ROT_STD_REF_NAME(DT_NODELABEL(base_rot_ref));
	const void *const inverted_rotation =
		&SENSOR_ROT_STD_REF_NAME(DT_NODELABEL(base_rot_inverted));

	/*
	 * Normally this gets set to rot-standard-ref during other init,
	 * which we aren't running in this test.
	 */
	motion_sensors[BASE_ACCEL].rot_standard_ref = normal_rotation;

	cros_cbi_get_fw_config_fake.custom_fake = get_base_orientation_normal;
	form_factor_init();
	zassert_equal_ptr(
		motion_sensors[BASE_ACCEL].rot_standard_ref, normal_rotation,
		"normal orientation should use the standard rotation matrix");

	RESET_FAKE(cros_cbi_get_fw_config);
	cros_cbi_get_fw_config_fake.return_val = EINVAL;
	form_factor_init();
	zassert_equal_ptr(motion_sensors[BASE_ACCEL].rot_standard_ref,
			  normal_rotation,
			  "errors should leave the rotation unchanged");

	cros_cbi_get_fw_config_fake.custom_fake = get_base_orientation_inverted;
	form_factor_init();
	zassert_equal_ptr(
		motion_sensors[BASE_ACCEL].rot_standard_ref, inverted_rotation,
		"inverted orientation should use the inverted rotation matrix");
}

ZTEST(nivviks, test_led_pwm)
{
	led_set_color_battery(EC_LED_COLOR_BLUE);
	zassert_equal(set_pwm_led_color_fake.arg0_val, PWM_LED0);
	zassert_equal(set_pwm_led_color_fake.arg1_val, EC_LED_COLOR_BLUE);

	led_set_color_battery(EC_LED_COLOR_AMBER);
	zassert_equal(set_pwm_led_color_fake.arg0_val, PWM_LED0);
	zassert_equal(set_pwm_led_color_fake.arg1_val, EC_LED_COLOR_AMBER);

	led_set_color_battery(EC_LED_COLOR_GREEN);
	zassert_equal(set_pwm_led_color_fake.arg0_val, PWM_LED0);
	zassert_equal(set_pwm_led_color_fake.arg1_val, -1);
}
