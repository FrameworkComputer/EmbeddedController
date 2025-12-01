/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "drivers/led.h"
#include "host_command.h"
#include "led_common.h"
#include "pwm_mock.h"
#include "test/drivers/test_state.h"

#include <zephyr/fff.h>
#include <zephyr/ztest.h>

/* these are tests specific to pwm LEDs.
 * for testing general LED behavior, see led_driver.led_common
 */

ZTEST(led_host_cmd, test_host_command__query)
{
	/* Gets the brightness range for an LED */

	int ret;
	struct ec_response_led_control response;
	struct ec_params_led_control params = {
		.led_id = EC_LED_ID_LEFT_LED,
		.flags = EC_LED_FLAGS_QUERY,
	};

	/* Expected brightness levels per color channel for this LED */
	uint8_t expected_brightness_ranges[] = {
		[EC_LED_COLOR_RED] = 0,	    [EC_LED_COLOR_GREEN] = 0,
		[EC_LED_COLOR_BLUE] = 100,  [EC_LED_COLOR_YELLOW] = 0,
		[EC_LED_COLOR_WHITE] = 100, [EC_LED_COLOR_AMBER] = 0,
	};

	ret = ec_cmd_led_control_v1(NULL, &params, &response);

	zassert_ok(ret, "Host command returned %d", ret);
	zassert_mem_equal(expected_brightness_ranges, response.brightness_range,
			  sizeof(expected_brightness_ranges), NULL);
}

ZTEST(led_host_cmd, test_host_command__manual_control)
{
	/* Set brightness for an LED directly */

	const struct device *pwm_blue_left =
		DEVICE_DT_GET(DT_NODELABEL(pwm_blue_left));
	const struct device *pwm_white_left =
		DEVICE_DT_GET(DT_NODELABEL(pwm_white_left));
	const struct device *pwm_amber_right =
		DEVICE_DT_GET(DT_NODELABEL(pwm_amber_right));
	const struct device *pwm_white_right =
		DEVICE_DT_GET(DT_NODELABEL(pwm_white_right));

	int ret;
	struct ec_response_led_control response;
	struct ec_params_led_control params = {
		.led_id = EC_LED_ID_LEFT_LED,
		.flags = 0x00,
		.brightness = {
			[EC_LED_COLOR_BLUE] = 50,
			/* All other color channels off */
		},
	};

	ret = ec_cmd_led_control_v1(NULL, &params, &response);

	zassert_equal(EC_RES_SUCCESS, ret, "Host command returned %d", ret);

	k_sleep(K_MSEC(100));

	zassert_equal(pwm_mock_get_duty(pwm_blue_left, 0), 50,
		      "LED should be 50%% blue but is instead %d%%",
		      pwm_mock_get_duty(pwm_blue_left, 0));
	zassert_equal(pwm_mock_get_duty(pwm_white_left, 0), 0, NULL);
	zassert_equal(pwm_mock_get_duty(pwm_amber_right, 0), 0, NULL);
	zassert_equal(pwm_mock_get_duty(pwm_white_right, 0), 0, NULL);

	params.flags = EC_LED_FLAGS_AUTO;
	ret = ec_cmd_led_control_v1(NULL, &params, &response);

	zassert_equal(EC_RES_SUCCESS, ret, "Host command returned %d", ret);
}

ZTEST_SUITE(led_host_cmd, drivers_predicate_post_main, NULL, NULL, NULL, NULL);
