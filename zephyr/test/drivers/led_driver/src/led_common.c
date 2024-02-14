/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "host_command.h"
#include "led_common.h"
#include "test/drivers/test_state.h"

#include <zephyr/fff.h>
#include <zephyr/ztest.h>

ZTEST(led_common, test_host_command__query)
{
	/* Gets the brightness range for an LED */

	int ret;
	struct ec_response_led_control response;
	struct ec_params_led_control params = {
		.led_id = EC_LED_ID_BATTERY_LED,
		.flags = EC_LED_FLAGS_QUERY,
	};

	/* Expected brightness levels per color channel for this LED */
	uint8_t expected_brightness_ranges[] = {
		[EC_LED_COLOR_RED] = 0,	  [EC_LED_COLOR_GREEN] = 0,
		[EC_LED_COLOR_BLUE] = 1,  [EC_LED_COLOR_YELLOW] = 0,
		[EC_LED_COLOR_WHITE] = 1, [EC_LED_COLOR_AMBER] = 0,
	};

	ret = ec_cmd_led_control_v1(NULL, &params, &response);

	zassert_ok(ret, "Host command returned %d", ret);
	zassert_mem_equal(expected_brightness_ranges, response.brightness_range,
			  sizeof(expected_brightness_ranges), NULL);
}

ZTEST(led_common, test_host_command__invalid_led)
{
	/* Try accessing info on a non-existent LED */

	int ret;
	struct ec_response_led_control response;
	struct ec_params_led_control params = {
		.led_id = EC_LED_ID_COUNT, /* Non-existent */
		.flags = EC_LED_FLAGS_QUERY,
	};

	ret = ec_cmd_led_control_v1(NULL, &params, &response);

	zassert_equal(EC_RES_INVALID_PARAM, ret, "Host command returned %d",
		      ret);
}

ZTEST(led_common, test_host_command__supported_channel)
{
	/* Try setting brightness on a color channel that is not supported */

	int ret;
	struct ec_response_led_control response;
	struct ec_params_led_control params = {
		.led_id = EC_LED_ID_BATTERY_LED,
		.flags = 0x00,
		.brightness = {
			/* This LED does not have a red channel */
			[EC_LED_COLOR_RED] = 100,
		},
	};

	ret = ec_cmd_led_control_v1(NULL, &params, &response);

	zassert_equal(EC_RES_INVALID_PARAM, ret, "Host command returned %d",
		      ret);
}

ZTEST(led_common, test_host_command__manual_control)
{
	/* Set brightness for an LED directly */

	int ret;
	struct ec_response_led_control response;
	struct ec_params_led_control params = {
		.led_id = EC_LED_ID_BATTERY_LED,
		.flags = 0x00,
		.brightness = {
			[EC_LED_COLOR_BLUE] = 1,
			/* All other color channels off */
		},
	};

	ret = ec_cmd_led_control_v1(NULL, &params, &response);

	zassert_equal(EC_RES_SUCCESS, ret, "Host command returned %d", ret);
	zassert_true(
		gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_chg_led_y_c0)),
		"LED blue channel is not on");
	zassert_false(
		gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_chg_led_w_c0)),
		"LED white channel is not off");
}

FAKE_VOID_FUNC(board_led_auto_control);

ZTEST(led_common, test_host_command__auto_control)
{
	/* Configure an LED for automatic control */

	int ret;
	struct ec_response_led_control response;
	struct ec_params_led_control params = {
		.led_id = EC_LED_ID_BATTERY_LED,
		.flags = EC_LED_FLAGS_AUTO,
	};

	ret = ec_cmd_led_control_v1(NULL, &params, &response);

	zassert_equal(EC_RES_SUCCESS, ret, "Host command returned %d", ret);
	zassert_equal(1, board_led_auto_control_fake.call_count,
		      "Did not call auto control function.");
}

static void reset(void *data)
{
	ARG_UNUSED(data);

	RESET_FAKE(board_led_auto_control);
}

ZTEST_SUITE(led_common, drivers_predicate_post_main, NULL, reset, reset, NULL);
