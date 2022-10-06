/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/shell/shell.h>
#include <zephyr/ztest.h>

#include "console.h"
#include "test/drivers/test_state.h"

static void set_wp(bool value)
{
	const struct gpio_dt_spec *wp = GPIO_DT_FROM_NODELABEL(gpio_wp_l);

	gpio_pin_set_dt(wp, value);
}

static void before(void *unused)
{
	/* Ensure eeprom is ready */
	set_wp(false);
	zassert_ok(shell_execute_cmd(get_ec_shell(), "cbi remove 42 init"),
		   NULL);
}

static void after(void *unused)
{
	/* re-enable WP */
	set_wp(true);
}

ZTEST_SUITE(console_cmd_cbi, drivers_predicate_post_main, NULL, before, after,
	    NULL);

ZTEST_USER(console_cmd_cbi, test_base)
{
	zassert_ok(shell_execute_cmd(get_ec_shell(), "cbi"), NULL);
}

ZTEST_USER(console_cmd_cbi, test_wp)
{
	set_wp(true);
	zassert_ok(!shell_execute_cmd(get_ec_shell(), "cbi remove 42"), NULL);
}

ZTEST_USER(console_cmd_cbi, test_remove)
{
	zassert_ok(!shell_execute_cmd(get_ec_shell(), "cbi remove"), NULL);
	zassert_ok(shell_execute_cmd(get_ec_shell(), "cbi remove 42"), NULL);
	zassert_ok(!shell_execute_cmd(get_ec_shell(), "cbi remove abc"), NULL);
	zassert_ok(!shell_execute_cmd(get_ec_shell(), "cbi remove 42 1"), NULL);
}

ZTEST_USER(console_cmd_cbi, test_set)
{
	zassert_ok(!shell_execute_cmd(get_ec_shell(), "cbi set"), NULL);
	zassert_ok(!shell_execute_cmd(get_ec_shell(), "cbi set 10"), NULL);
	zassert_ok(!shell_execute_cmd(get_ec_shell(), "cbi set 11 1"), NULL);
	zassert_ok(shell_execute_cmd(get_ec_shell(), "cbi set 12 1 4"), NULL);
	zassert_ok(!shell_execute_cmd(get_ec_shell(), "cbi set 13 1 4 4"),
		   NULL);
	zassert_ok(!shell_execute_cmd(get_ec_shell(), "cbi set 14 1 10"), NULL);
}

ZTEST_USER(console_cmd_cbi, test_extra)
{
	zassert_ok(shell_execute_cmd(get_ec_shell(),
				     "cbi remove 42 skip_write"),
		   NULL);
	zassert_ok(shell_execute_cmd(get_ec_shell(), "cbi remove 42 init"),
		   NULL);
	zassert_ok(shell_execute_cmd(get_ec_shell(),
				     "cbi remove 42 init skip_write"),
		   NULL);
	zassert_ok(shell_execute_cmd(get_ec_shell(),
				     "cbi remove 42 skip_write init"),
		   NULL);
	zassert_ok(!shell_execute_cmd(get_ec_shell(), "cbi remove 42 extra"),
		   NULL);
}
