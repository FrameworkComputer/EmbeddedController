/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <stdint.h>
#include <zephyr/ztest.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest_assert.h>

#include "host_command.h"
#include "keyboard_backlight.h"
#include "test/drivers/test_state.h"

/**
 * @brief Send host command to set the backlight percentage
 *
 * @param percent Backlight intensity, from 0 to 100 (inclusive).
 * @return uint16_t Host command return code
 */
static uint16_t set_backlight_percent_helper(uint8_t percent)
{
	struct ec_params_pwm_set_keyboard_backlight params = {
		.percent = percent
	};
	struct host_cmd_handler_args args = BUILD_HOST_COMMAND_PARAMS(
		EC_CMD_PWM_SET_KEYBOARD_BACKLIGHT, 0, params);

	return host_command_process(&args);
}

ZTEST(keyboard_backlight, host_command_set_backlight__normal)
{
	/* Set the backlight intensity level to this and verify */
	uint8_t expected_percentage = 50;

	zassert_ok(set_backlight_percent_helper(expected_percentage), NULL);
	zassert_equal(expected_percentage, kblight_get(), NULL);
}

ZTEST(keyboard_backlight, host_command_set_backlight__out_of_range)
{
	/* Too high */
	uint8_t expected_percentage = 101;

	zassert_equal(EC_RES_ERROR,
		      set_backlight_percent_helper(expected_percentage), NULL);
}

static void reset(void *data)
{
	ARG_UNUSED(data);

	/* Reset the backlight to off and 0% brightness */
	kblight_set(0);
	kblight_enable(0);
}

ZTEST_SUITE(keyboard_backlight, drivers_predicate_post_main, NULL, reset, reset,
	    NULL);
