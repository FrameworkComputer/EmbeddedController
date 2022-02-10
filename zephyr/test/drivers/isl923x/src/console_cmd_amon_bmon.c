/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <drivers/adc.h>
#include <drivers/adc/adc_emul.h>
#include <shell/shell.h>
#include <ztest.h>

#include "adc.h"
#include "console.h"
#include "charger_utils.h"
#include "driver/charger/isl923x.h"
#include "driver/charger/isl923x_public.h"
#include "ec_commands.h"
#include "emul/emul_common_i2c.h"
#include "emul/emul_isl923x.h"
#include "test_state.h"
#include "utils.h"

#define ADC_DEVICE_NODE DT_NODELABEL(adc0)
#define CHARGER_NUM get_charger_num(&isl923x_drv)
#define ISL923X_EMUL emul_get_binding(DT_LABEL(DT_NODELABEL(isl923x_emul)))

ZTEST_SUITE(console_cmd_amon_bmon, drivers_predicate_post_main, NULL, NULL,
	    NULL, NULL);

ZTEST(console_cmd_amon_bmon, test_isl923x_amonbmon_shell_cmd)
{
	/* Validate combinations of well formed shell commands */
	zassert_ok(shell_execute_cmd(get_ec_shell(), "amonbmon a 0"), NULL);
	zassert_ok(shell_execute_cmd(get_ec_shell(), "amonbmon ac 0"), NULL);
	zassert_ok(shell_execute_cmd(get_ec_shell(), "amonbmon ad 0"), NULL);

	zassert_ok(shell_execute_cmd(get_ec_shell(), "amonbmon b 0"), NULL);
	zassert_ok(shell_execute_cmd(get_ec_shell(), "amonbmon bc 0"), NULL);
	zassert_ok(shell_execute_cmd(get_ec_shell(), "amonbmon bd 0"), NULL);

	/* Check error returned for malformed shell command */
	zassert_equal(EC_ERROR_PARAM2,
		      shell_execute_cmd(get_ec_shell(), "amonbmon a x"), NULL);
}

ZTEST(console_cmd_amon_bmon, test_isl923x_amonbmon_get_input_current)
{
	const struct device *adc_dev = DEVICE_DT_GET(ADC_DEVICE_NODE);
	const uint16_t input_mv = 1000;
	int current_milli_amps, ret;

	ret = adc_emul_const_value_set(adc_dev, ADC_AMON_BMON, input_mv);
	zassert_ok(ret, "adc_emul_const_value_set() failed with code %d", ret);

	zassert_ok(isl923x_drv.get_input_current(CHARGER_NUM,
						 &current_milli_amps),
		   NULL);
	zassert_within(current_milli_amps, 5000, 10,
		       "Expected input current %dmA but got %dmA", 5000,
		       current_milli_amps);
}

ZTEST(console_cmd_amon_bmon,
	test_isl923x_amonbmon_get_input_current_read_fail_req1)
{
	const struct emul *isl923x_emul = ISL923X_EMUL;
	struct i2c_emul *i2c_emul = isl923x_emul_get_i2c_emul(isl923x_emul);
	int current_milli_amps;

	i2c_common_emul_set_read_fail_reg(i2c_emul, ISL923X_REG_CONTROL1);
	zassert_equal(EC_ERROR_INVAL,
		      isl923x_drv.get_input_current(CHARGER_NUM,
						    &current_milli_amps),
		      NULL);
	zassert_equal(EC_ERROR_INVAL,
		      shell_execute_cmd(get_ec_shell(), "amonbmon a 0"), NULL);
}

ZTEST(console_cmd_amon_bmon,
	test_isl923x_amonbmon_get_input_current_read_fail_req3)
{
	const struct emul *isl923x_emul = ISL923X_EMUL;
	struct i2c_emul *i2c_emul = isl923x_emul_get_i2c_emul(isl923x_emul);
	int current_milli_amps;

	i2c_common_emul_set_read_fail_reg(i2c_emul, ISL9238_REG_CONTROL3);
	zassert_equal(EC_ERROR_INVAL,
		      isl923x_drv.get_input_current(CHARGER_NUM,
						    &current_milli_amps),
		      NULL);
}

ZTEST(console_cmd_amon_bmon,
	test_isl923x_amonbmon_get_input_current_write_fail_req1)
{
	const struct emul *isl923x_emul = ISL923X_EMUL;
	struct i2c_emul *i2c_emul = isl923x_emul_get_i2c_emul(isl923x_emul);
	int current_milli_amps;

	i2c_common_emul_set_write_fail_reg(i2c_emul, ISL923X_REG_CONTROL1);
	zassert_equal(EC_ERROR_INVAL,
		      isl923x_drv.get_input_current(CHARGER_NUM,
						    &current_milli_amps),
		      NULL);
}

ZTEST(console_cmd_amon_bmon,
	test_isl923x_amonbmon_get_input_current_write_fail_req3)
{
	const struct emul *isl923x_emul = ISL923X_EMUL;
	struct i2c_emul *i2c_emul = isl923x_emul_get_i2c_emul(isl923x_emul);
	int current_milli_amps;

	i2c_common_emul_set_write_fail_reg(i2c_emul, ISL9238_REG_CONTROL3);
	zassert_equal(EC_ERROR_INVAL,
		      isl923x_drv.get_input_current(CHARGER_NUM,
						    &current_milli_amps),
		      NULL);
}
