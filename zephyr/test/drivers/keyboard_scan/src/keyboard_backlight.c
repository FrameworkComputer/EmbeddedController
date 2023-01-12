/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "console.h"
#include "host_command.h"
#include "keyboard_backlight.h"
#include "pwm_mock.h"
#include "test/drivers/test_state.h"

#include <stdint.h>
#include <string.h>

#include <zephyr/drivers/pwm.h>
#include <zephyr/kernel.h>
#include <zephyr/shell/shell_dummy.h>
#include <zephyr/ztest.h>
#include <zephyr/ztest_assert.h>

#define KBLIGHT_PWM_NODE DT_COMPAT_GET_ANY_STATUS_OKAY(cros_ec_kblight_pwm)

extern const struct kblight_drv kblight_pwm;

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

	return ec_cmd_pwm_set_keyboard_backlight(NULL, &params);
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

ZTEST(keyboard_backlight, host_command_get_backlight__normal)
{
	/* Set this backlight intensity and verify via host command */
	uint8_t expected_percentage = 50;
	int ret;

	zassert_ok(set_backlight_percent_helper(expected_percentage), NULL);

	/* Brief delay to allow a deferred function to enable the backlight */
	k_sleep(K_MSEC(50));

	struct ec_response_pwm_get_keyboard_backlight response;

	ret = ec_cmd_pwm_get_keyboard_backlight(NULL, &response);
	zassert_ok(ret, "Host command failed: %d", ret);
	zassert_equal(expected_percentage, response.percent, NULL);
	zassert_equal(1, response.enabled, "Got 0x%02x", response.enabled);
}

ZTEST(keyboard_backlight, console_command__noargs)
{
	/* Command should print current status. Set backlight on and to 70% */

	const char *outbuffer;
	size_t buffer_size;

	zassert_ok(set_backlight_percent_helper(70), NULL);
	k_sleep(K_MSEC(50));

	/* With no args, print current state */
	shell_backend_dummy_clear_output(get_ec_shell());
	zassert_ok(shell_execute_cmd(get_ec_shell(), "kblight"), NULL);
	outbuffer =
		shell_backend_dummy_get_output(get_ec_shell(), &buffer_size);

	zassert_ok(!strstr(outbuffer, "Keyboard backlight: 70% enabled: 1"),
		   "Actual string: `%s`", outbuffer);
}

ZTEST(keyboard_backlight, console_command__set_on)
{
	/* Command should enable backlight to given intensity */

	zassert_ok(shell_execute_cmd(get_ec_shell(), "kblight 65"), NULL);
	zassert_equal(65, kblight_get(), NULL);
	zassert_equal(1, kblight_get_current_enable(), NULL);
}

ZTEST(keyboard_backlight, console_command__set_off)
{
	zassert_ok(set_backlight_percent_helper(40), NULL);
	k_sleep(K_MSEC(50));

	/* Turn back off */
	zassert_ok(shell_execute_cmd(get_ec_shell(), "kblight 0"), NULL);
	zassert_equal(0, kblight_get(), NULL);
	zassert_equal(0, kblight_get_current_enable(), NULL);
}

ZTEST(keyboard_backlight, console_command__bad_params)
{
	zassert_equal(EC_ERROR_PARAM1,
		      shell_execute_cmd(get_ec_shell(), "kblight NaN"), NULL);
	zassert_equal(EC_ERROR_PARAM1,
		      shell_execute_cmd(get_ec_shell(), "kblight -1"), NULL);
	zassert_equal(EC_ERROR_PARAM1,
		      shell_execute_cmd(get_ec_shell(), "kblight 101"), NULL);
}

ZTEST(keyboard_backlight, set_backlight__device_not_ready)
{
	const struct pwm_dt_spec kblight_pwm_dt =
		PWM_DT_SPEC_GET(KBLIGHT_PWM_NODE);
	const struct device *pwm_dev = kblight_pwm_dt.dev;
	int initial_duty;
	int initialized_saved;

	initial_duty = pwm_mock_get_duty(pwm_dev, kblight_pwm_dt.channel);

	initialized_saved = pwm_dev->state->initialized;
	pwm_dev->state->initialized = 0;

	zassert_ok(kblight_pwm.set(initial_duty + 10), NULL);
	zassert_equal(initial_duty,
		      pwm_mock_get_duty(pwm_dev, kblight_pwm_dt.channel), NULL);

	pwm_dev->state->initialized = initialized_saved;
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
