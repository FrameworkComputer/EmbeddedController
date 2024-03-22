/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "ec_commands.h"
#include "led.h"
#include "led_common.h"
#include "pwm_mock.h"
#include "test/drivers/test_state.h"
#include "test/drivers/utils.h"

#include <zephyr/device.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/ztest.h>
#include <zephyr/ztest_assert.h>

ZTEST_SUITE(pwm_led_driver, drivers_predicate_post_main, NULL, NULL, NULL,
	    NULL);

ZTEST(pwm_led_driver, test_led_set_brightness)
{
	const uint8_t brightness_off[EC_LED_COLOR_COUNT] = {};
	const uint8_t brightness_white[EC_LED_COLOR_COUNT] = {
		[EC_LED_COLOR_WHITE] = 1
	};
	const uint8_t brightness_amber[EC_LED_COLOR_COUNT] = {
		[EC_LED_COLOR_AMBER] = 1
	};
	const uint8_t brightness_yellow[EC_LED_COLOR_COUNT] = {
		[EC_LED_COLOR_YELLOW] = 1
	};
	const struct device *pwm_blue_left =
		DEVICE_DT_GET(DT_NODELABEL(pwm_blue_left));
	const struct device *pwm_white_left =
		DEVICE_DT_GET(DT_NODELABEL(pwm_white_left));
	const struct device *pwm_amber_right =
		DEVICE_DT_GET(DT_NODELABEL(pwm_amber_right));
	const struct device *pwm_white_right =
		DEVICE_DT_GET(DT_NODELABEL(pwm_white_right));

	/* Turn off all LEDs */
	led_set_brightness(EC_LED_ID_LEFT_LED, brightness_off);
	led_set_brightness(EC_LED_ID_RIGHT_LED, brightness_off);
	zassert_equal(pwm_mock_get_duty(pwm_blue_left, 0), 0, NULL);
	zassert_equal(pwm_mock_get_duty(pwm_white_left, 0), 0, NULL);
	zassert_equal(pwm_mock_get_duty(pwm_amber_right, 0), 0, NULL);
	zassert_equal(pwm_mock_get_duty(pwm_white_right, 0), 0, NULL);

	/* Call led_set_color(LED_WHITE, LEFT_LED) */
	led_set_brightness(EC_LED_ID_LEFT_LED, brightness_white);
	zassert_equal(pwm_mock_get_duty(pwm_blue_left, 0), 0, NULL);
	zassert_equal(pwm_mock_get_duty(pwm_white_left, 0), 100, NULL);
	zassert_equal(pwm_mock_get_duty(pwm_amber_right, 0), 0, NULL);
	zassert_equal(pwm_mock_get_duty(pwm_white_right, 0), 0, NULL);

	/* Unsupporte, call led_set_color(LED_OFF, LEFT_LED) */
	led_set_brightness(EC_LED_ID_LEFT_LED, brightness_amber);
	zassert_equal(pwm_mock_get_duty(pwm_blue_left, 0), 0, NULL);
	zassert_equal(pwm_mock_get_duty(pwm_white_left, 0), 0, NULL);
	zassert_equal(pwm_mock_get_duty(pwm_amber_right, 0), 0, NULL);
	zassert_equal(pwm_mock_get_duty(pwm_white_right, 0), 0, NULL);

	/* Call led_set_color(AMBER, RIGHT_LED) */
	led_set_brightness(EC_LED_ID_RIGHT_LED, brightness_amber);
	zassert_equal(pwm_mock_get_duty(pwm_blue_left, 0), 0, NULL);
	zassert_equal(pwm_mock_get_duty(pwm_white_left, 0), 0, NULL);
	zassert_equal(pwm_mock_get_duty(pwm_amber_right, 0), 100, NULL);
	zassert_equal(pwm_mock_get_duty(pwm_white_right, 0), 0, NULL);

	/* Call led_set_color(YELLOW, RIGHT_LED) */
	led_set_brightness(EC_LED_ID_RIGHT_LED, brightness_yellow);
	zassert_equal(pwm_mock_get_duty(pwm_blue_left, 0), 0, NULL);
	zassert_equal(pwm_mock_get_duty(pwm_white_left, 0), 0, NULL);
	zassert_equal(pwm_mock_get_duty(pwm_amber_right, 0), 100, NULL);
	zassert_equal(pwm_mock_get_duty(pwm_white_right, 0), 20, NULL);
}

ZTEST(pwm_led_driver, test_led_get_brightness)
{
	uint8_t brightness[EC_LED_COLOR_COUNT];
	const uint8_t expected_left[EC_LED_COLOR_COUNT] = {
		[EC_LED_COLOR_BLUE] = 100,
		[EC_LED_COLOR_WHITE] = 100,
	};
	const uint8_t expected_right[EC_LED_COLOR_COUNT] = {
		[EC_LED_COLOR_WHITE] = 100,
		[EC_LED_COLOR_AMBER] = 100,
		[EC_LED_COLOR_YELLOW] = 100,
	};

	/* Verify LED colors defined in device tree are reflected in the
	 * brightness array.
	 */
	memset(brightness, 255, sizeof(brightness));
	led_get_brightness_range(EC_LED_ID_LEFT_LED, brightness);
	zassert_mem_equal(brightness, expected_left, sizeof(brightness), NULL);

	memset(brightness, 255, sizeof(brightness));
	led_get_brightness_range(EC_LED_ID_RIGHT_LED, brightness);
	zassert_mem_equal(brightness, expected_right, sizeof(brightness), NULL);
}
