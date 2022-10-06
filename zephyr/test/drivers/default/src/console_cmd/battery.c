/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/drivers/emul.h>
#include <zephyr/shell/shell.h>
#include <zephyr/ztest.h>

#include "battery_smart.h"
#include "console.h"
#include "ec_commands.h"
#include "emul/emul_common_i2c.h"
#include "emul/emul_smart_battery.h"
#include "test/drivers/test_state.h"
#include "test/drivers/utils.h"

struct console_cmd_battery_fixture {
	const struct emul *emul;
	struct i2c_common_emul_data *i2c_emul;
};

static void *console_cmd_battery_setup(void)
{
	static struct console_cmd_battery_fixture fixture = {
		.emul = EMUL_DT_GET(DT_NODELABEL(battery)),
	};

	fixture.i2c_emul = emul_smart_battery_get_i2c_common_data(fixture.emul);

	return &fixture;
}

static void console_cmd_battery_after(void *f)
{
	struct console_cmd_battery_fixture *fixture = f;

	i2c_common_emul_set_read_fail_reg(fixture->i2c_emul,
					  I2C_COMMON_EMUL_NO_FAIL_REG);
}

/* Default battery command */
ZTEST_USER(console_cmd_battery, test_battery_default)
{
	zassert_ok(shell_execute_cmd(get_ec_shell(), "battery"),
		   "Failed default print");
}

ZTEST_USER_F(console_cmd_battery, test_battery_status_i2c_error)
{
	/* Force a failure on the battery i2c write to SB_BATTERY_STATUS */
	i2c_common_emul_set_read_fail_reg(fixture->i2c_emul, SB_BATTERY_STATUS);
	zassert_ok(shell_execute_cmd(get_ec_shell(), "battery"),
		   "Failed default print");
}

/* Battery command with repeat */
ZTEST_USER(console_cmd_battery, test_battery_repeat)
{
	zassert_ok(shell_execute_cmd(get_ec_shell(), "battery 2"),
		   "Failed default print");
	zassert_ok(shell_execute_cmd(get_ec_shell(), "battery 8"),
		   "Failed default print");
}

/* Battery command with repeat and sleep */
ZTEST_USER(console_cmd_battery, test_battery_repeat_sleep)
{
	zassert_ok(shell_execute_cmd(get_ec_shell(), "battery 2 400"),
		   "Failed default print");
	zassert_ok(shell_execute_cmd(get_ec_shell(), "battery 8 200"),
		   "Failed default print");
}

/* Battery command with invalid repeat and sleep */
ZTEST_USER(console_cmd_battery, test_battery_bad_repeat_sleep)
{
	int rv = shell_execute_cmd(get_ec_shell(), "battery fish 400");

	zassert_equal(rv, EC_ERROR_INVAL, "Expected %d, but got %d",
		      EC_ERROR_INVAL, rv);

	rv = shell_execute_cmd(get_ec_shell(), "battery 2 fish");

	zassert_equal(rv, EC_ERROR_INVAL, "Expected %d, but got %d",
		      EC_ERROR_INVAL, rv);
}

ZTEST_SUITE(console_cmd_battery, drivers_predicate_post_main,
	    console_cmd_battery_setup, NULL, console_cmd_battery_after, NULL);
