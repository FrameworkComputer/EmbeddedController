/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/shell/shell.h>
#include <zephyr/ztest.h>

#include "console.h"
#include "ec_commands.h"
#include "test/drivers/test_state.h"
#include "test/drivers/utils.h"

ZTEST_USER(console_cmd_gpio, test_read_invoke_success)
{
	zassert_ok(shell_execute_cmd(get_ec_shell(), "gpioget test"), NULL);
}

ZTEST_USER(console_cmd_gpio, test_read_invoke_fail)
{
	zassert_ok(!shell_execute_cmd(get_ec_shell(), "gpioget DOES_NOT_EXIST"),
		   NULL);
}

ZTEST_USER(console_cmd_gpio, test_set_gpio)
{
	const struct gpio_dt_spec *gp = GPIO_DT_FROM_NODELABEL(gpio_test);

	zassert_ok(gpio_pin_set_dt(gp, 0), NULL);
	zassert_ok(shell_execute_cmd(get_ec_shell(), "gpioset test 1"), NULL);
	zassert_equal(gpio_pin_get_dt(gp), 1, NULL);
	zassert_ok(shell_execute_cmd(get_ec_shell(), "gpioset test 0"), NULL);
	zassert_equal(gpio_pin_get_dt(gp), 0, NULL);
}

ZTEST_SUITE(console_cmd_gpio, drivers_predicate_post_main, NULL, NULL, NULL,
	    NULL);
