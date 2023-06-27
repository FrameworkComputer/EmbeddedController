/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "drivers/cros_displight.h"
#include "ec_commands.h"
#include "host_command.h"
#include "keyboard_backlight.h"
#include "pwm.h"
#include "test/drivers/test_state.h"

#include <zephyr/drivers/emul.h>
#include <zephyr/drivers/i2c_emul.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>
#include <zephyr/ztest_assert.h>

ZTEST(shim_pwm_hc, test_pwm_set_duty_hc__kblight)
{
	struct ec_params_pwm_set_duty p = {
		.index = DT_REG_ADDR(DT_NODELABEL(pwm_kblight)),
		.pwm_type = EC_PWM_TYPE_KB_LIGHT,
		/* Arbitrary 56% */
		.duty = PWM_PERCENT_TO_RAW(56),
	};

	zassert_ok(ec_cmd_pwm_set_duty(NULL, &p));
	zassert_equal(kblight_get(), PWM_RAW_TO_PERCENT(p.duty));
}

ZTEST(shim_pwm_hc, test_pwm_set_duty_hc__displight)
{
	struct ec_params_pwm_set_duty p = {
		.index = DT_REG_ADDR(DT_NODELABEL(pwm_displight)),
		.pwm_type = EC_PWM_TYPE_DISPLAY_LIGHT,
		/* Arbitrary 72% */
		.duty = PWM_PERCENT_TO_RAW(72)
	};

	zassert_ok(ec_cmd_pwm_set_duty(NULL, &p));
	zassert_equal(displight_get(), PWM_RAW_TO_PERCENT(p.duty));
}

ZTEST(shim_pwm_hc, test_pwm_set_duty_hc__bad_pwm_type)
{
	struct ec_params_pwm_set_duty p = {
		/* Arbitrary, don't care */
		p.index = 0,
		/* PWM Type doesn't actually exist */
		p.pwm_type = EC_PWM_TYPE_COUNT,
	};

	zassert_equal(EC_RES_INVALID_PARAM, ec_cmd_pwm_set_duty(NULL, &p));
}

ZTEST(shim_pwm_hc, test_pwm_get_duty_hc__kblight)
{
	struct ec_params_pwm_get_duty p = {
		.index = DT_REG_ADDR(DT_NODELABEL(pwm_kblight)),
		.pwm_type = EC_PWM_TYPE_KB_LIGHT,
	};

	struct ec_response_pwm_get_duty r;
	struct host_cmd_handler_args args;

	/* Set kblight percentage to arbitrary 56% */
	kblight_set(56);

	zassert_ok(ec_cmd_pwm_get_duty(&args, &p, &r));
	zassert_equal(args.response_size, sizeof(r));
	zassert_equal(r.duty, PWM_PERCENT_TO_RAW(56));
}

ZTEST(shim_pwm_hc, test_pwm_get_duty_hc__displight)
{
	struct ec_params_pwm_get_duty p = {
		p.index = DT_REG_ADDR(DT_NODELABEL(pwm_displight)),
		p.pwm_type = EC_PWM_TYPE_DISPLAY_LIGHT,
	};

	struct ec_response_pwm_get_duty r;
	struct host_cmd_handler_args args;

	/* Set displight percentage to arbitrary 72% */
	displight_set(72);

	zassert_ok(ec_cmd_pwm_get_duty(&args, &p, &r));
	zassert_equal(args.response_size, sizeof(r));
	zassert_equal(r.duty, PWM_PERCENT_TO_RAW(72));
}

ZTEST_SUITE(shim_pwm_hc, drivers_predicate_post_main, NULL, NULL, NULL, NULL);
