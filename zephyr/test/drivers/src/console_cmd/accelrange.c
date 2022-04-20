/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <shell/shell.h>
#include <devicetree.h>
#include <ztest.h>

#include "console.h"
#include "driver/accel_bma2x2.h"
#include "ec_commands.h"
#include "emul/emul_bma255.h"
#include "emul/emul_common_i2c.h"
#include "i2c.h"
#include "motion_sense.h"
#include "test/drivers/test_state.h"

#define EMUL_LABEL DT_NODELABEL(bma_emul)

#define BMA_ORD DT_DEP_ORD(EMUL_LABEL)

static void console_cmd_accelrange_after(void *fixture)
{
	struct i2c_emul *emul = bma_emul_get(BMA_ORD);

	ARG_UNUSED(fixture);
	shell_execute_cmd(get_ec_shell(), "accelrange 0 2");
	i2c_common_emul_set_read_fail_reg(emul, I2C_COMMON_EMUL_NO_FAIL_REG);
}

ZTEST_SUITE(console_cmd_accelrange, drivers_predicate_post_main, NULL, NULL,
	    console_cmd_accelrange_after, NULL);

ZTEST_USER(console_cmd_accelrange, test_num_args)
{
	int rv;

	rv = shell_execute_cmd(get_ec_shell(), "accelrange");
	zassert_equal(rv, EC_ERROR_PARAM_COUNT, "Expected %d, but got %d",
		      EC_ERROR_PARAM_COUNT, rv);

	rv = shell_execute_cmd(get_ec_shell(), "accelrange 0 1 2 3");
	zassert_equal(rv, EC_ERROR_PARAM_COUNT, "Expected %d, but got %d",
		      EC_ERROR_PARAM_COUNT, rv);
}

ZTEST_USER(console_cmd_accelrange, test_bad_sensor_num)
{
	int rv;

	rv = shell_execute_cmd(get_ec_shell(), "accelrange t");
	zassert_equal(rv, EC_ERROR_PARAM1, "Expected %d, but got %d",
		      EC_ERROR_PARAM1, rv);

	rv = shell_execute_cmd(get_ec_shell(), "accelrange -1");
	zassert_equal(rv, EC_ERROR_PARAM1, "Expected %d, but got %d",
		      EC_ERROR_PARAM1, rv);

	rv = shell_execute_cmd(get_ec_shell(), "accelrange 100");
	zassert_equal(rv, EC_ERROR_PARAM1, "Expected %d, but got %d",
		      EC_ERROR_PARAM1, rv);
}

ZTEST_USER(console_cmd_accelrange, test_print_range)
{
	zassert_ok(shell_execute_cmd(get_ec_shell(), "accelrange 0"), NULL);
}

ZTEST_USER(console_cmd_accelrange, test_set_invalid_range)
{
	int rv = shell_execute_cmd(get_ec_shell(), "accelrange 0 t");

	zassert_equal(rv, EC_ERROR_PARAM2, "Expected %d, but got %d",
		      EC_ERROR_PARAM2, rv);
}

ZTEST_USER(console_cmd_accelrange, test_set_range_round_up_implicit)
{
	zassert_ok(shell_execute_cmd(get_ec_shell(), "accelrange 0 3"), NULL);
	zassert_equal(motion_sensors[0].current_range, 4,
		      "Expected 4, but got %d",
		      motion_sensors[0].current_range);
}

ZTEST_USER(console_cmd_accelrange, test_set_range_round_up_explicit)
{
	zassert_ok(shell_execute_cmd(get_ec_shell(), "accelrange 0 3 1"), NULL);
	zassert_equal(motion_sensors[0].current_range, 4,
		      "Expected 4, but got %d",
		      motion_sensors[0].current_range);
}

ZTEST_USER(console_cmd_accelrange, test_set_range_round_down)
{
	zassert_ok(shell_execute_cmd(get_ec_shell(), "accelrange 0 5 0"), NULL);
	zassert_equal(motion_sensors[0].current_range, 4,
		      "Expected 4, but got %d",
		      motion_sensors[0].current_range);
}

ZTEST_USER(console_cmd_accelrange, test_i2c_error)
{
	struct i2c_emul *emul = bma_emul_get(BMA_ORD);
	int rv;

	i2c_common_emul_set_read_fail_reg(emul, BMA2x2_RANGE_SELECT_ADDR);

	rv = shell_execute_cmd(get_ec_shell(), "accelrange 0 3");
	zassert_equal(rv, EC_ERROR_PARAM2, "Expected %d, but got %d",
		      EC_ERROR_PARAM2, rv);
}
