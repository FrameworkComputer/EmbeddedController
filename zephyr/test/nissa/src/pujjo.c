/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "battery.h"
#include "button.h"
#include "cros_cbi.h"
#include "ec_commands.h"
#include "hooks.h"
#include "keyboard_protocol.h"
#include "led_common.h"
#include "led_onoff_states.h"
#include "motionsense_sensors.h"
#include "pujjo.h"
#include "pwm_mock.h"
#include "tablet_mode.h"

#include <zephyr/fff.h>
#include <zephyr/logging/log.h>
#include <zephyr/ztest.h>

LOG_MODULE_REGISTER(nissa, LOG_LEVEL_INF);

FAKE_VALUE_FUNC(int, cros_cbi_get_fw_config, enum cbi_fw_config_field_id,
		uint32_t *);
FAKE_VALUE_FUNC(enum battery_present, battery_is_present);
FAKE_VOID_FUNC(board_set_active_charge_port, int);
FAKE_VOID_FUNC(fan_set_count, int);
FAKE_VOID_FUNC(usb_interrupt_c1, enum gpio_signal);

int button_disable_gpio(enum button button_type)
{
	return EC_SUCCESS;
}

static void pujjo_test_before(void *fixture)
{
	RESET_FAKE(cros_cbi_get_fw_config);
	RESET_FAKE(battery_is_present);
	RESET_FAKE(board_set_active_charge_port);
	RESET_FAKE(fan_set_count);
}

ZTEST_SUITE(pujjo, NULL, NULL, pujjo_test_before, NULL, NULL);

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

ZTEST(pujjo, test_fan_present)
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

ZTEST(pujjo, test_fan_absent)
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

ZTEST(pujjo, test_fan_cbi_error)
{
	int flags;

	cros_cbi_get_fw_config_fake.return_val = EINVAL;
	fan_init();

	zassert_equal(fan_set_count_fake.call_count, 0);
	zassert_ok(gpio_pin_get_config_dt(
		GPIO_DT_FROM_NODELABEL(gpio_fan_enable), &flags));
	zassert_equal(flags, 0, "actual GPIO flags were %#x", flags);
}

ZTEST(pujjo, test_led)
{
	/* led pin is low active, status 0 is turn on */
	led_set_color_battery(EC_LED_COLOR_AMBER);
	zassert_false(gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_led_1_odl)),
		      "LED_1 is not on");
	zassert_false(gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_led_2_odl)),
		      "LED_2 is not on");
	/*
	 * Case for led pin is untestable because emulated GPIOs don't
	 * allow getting the current value of output pins.
	 */
}
