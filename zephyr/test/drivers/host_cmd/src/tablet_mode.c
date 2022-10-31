/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/drivers/emul.h>
#include <zephyr/ztest.h>

#include "hooks.h"
#include "host_command.h"
#include "tablet_mode.h"
#include "emul/emul_common_i2c.h"
#include "emul/emul_smart_battery.h"
#include "test/drivers/test_state.h"
#include "test/drivers/utils.h"

struct host_cmd_tablet_mode_fixture {
	const struct emul *emul;
	struct i2c_common_emul_data *i2c_emul;
};

static void host_cmd_tablet_mode_before(void *f)
{
	ARG_UNUSED(f);
	tablet_reset();
}

static void host_cmd_tablet_mode_after(void *f)
{
	ARG_UNUSED(f);
	tablet_reset();
}

ZTEST_SUITE(host_cmd_tablet_mode, drivers_predicate_post_main, NULL,
	    host_cmd_tablet_mode_before, host_cmd_tablet_mode_after, NULL);

/* Test tablet mode can be enabled with a host command. */
ZTEST_USER_F(host_cmd_tablet_mode, test_tablet_mode_on)
{
	int rv;
	struct ec_params_set_tablet_mode params = {
		.tablet_mode = TABLET_MODE_FORCE_TABLET
	};

	struct host_cmd_handler_args args = BUILD_HOST_COMMAND_PARAMS(
		EC_CMD_SET_TABLET_MODE, UINT8_C(0), params);

	rv = host_command_process(&args);
	zassert_equal(EC_RES_SUCCESS, rv, "Expected EC_RES_SUCCESS, but got %d",
		      rv);
	/* Return 1 if in tablet mode, 0 otherwise */
	rv = tablet_get_mode();
	zassert_equal(rv, 1, "unexpected tablet mode: %d", rv);
}

/* Test tablet mode can be disabled with a host command. */
ZTEST_USER_F(host_cmd_tablet_mode, test_tablet_mode_off)
{
	int rv;
	struct ec_params_set_tablet_mode params = {
		.tablet_mode = TABLET_MODE_FORCE_CLAMSHELL
	};

	struct host_cmd_handler_args args = BUILD_HOST_COMMAND_PARAMS(
		EC_CMD_SET_TABLET_MODE, UINT8_C(0), params);

	rv = host_command_process(&args);
	zassert_equal(EC_RES_SUCCESS, rv, "Expected EC_RES_SUCCESS, but got %d",
		      rv);
	/* Return 1 if in tablet mode, 0 otherwise */
	rv = tablet_get_mode();
	zassert_equal(rv, 0, "unexpected tablet mode: %d", rv);
}

/* Test tablet mode can be reset with a host command. */
ZTEST_USER_F(host_cmd_tablet_mode, test_tablet_mode_reset)
{
	int rv;
	struct ec_params_set_tablet_mode params = {
		.tablet_mode = TABLET_MODE_DEFAULT
	};

	struct host_cmd_handler_args args = BUILD_HOST_COMMAND_PARAMS(
		EC_CMD_SET_TABLET_MODE, UINT8_C(0), params);

	rv = host_command_process(&args);
	zassert_equal(EC_RES_SUCCESS, rv, "Expected EC_RES_SUCCESS, but got %d",
		      rv);
	/* Return 1 if in tablet mode, 0 otherwise */
	rv = tablet_get_mode();
	zassert_equal(rv, 0, "unexpected tablet mode: %d", rv);
}

/* Test tablet mode can handle invalid host command parameters. */
ZTEST_USER_F(host_cmd_tablet_mode, test_tablet_mode_invalid_parameter)
{
	int rv;
	struct ec_params_set_tablet_mode params = {
		.tablet_mode = 0xEE /* Sufficiently random, bad value.*/
	};

	struct host_cmd_handler_args args = BUILD_HOST_COMMAND_PARAMS(
		EC_CMD_SET_TABLET_MODE, UINT8_C(0), params);

	rv = host_command_process(&args);
	zassert_equal(EC_RES_INVALID_PARAM, rv,
		      "Expected EC_RES_INVALID_PARAM, but got %d", rv);
	/* Return 1 if in tablet mode, 0 otherwise */
	rv = tablet_get_mode();
	zassert_equal(rv, 0, "unexpected tablet mode: %d", rv);
}
