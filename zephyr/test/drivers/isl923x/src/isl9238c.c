/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "driver/charger/isl923x.h"
#include "driver/charger/isl923x_public.h"
#include "emul/emul_common_i2c.h"
#include "emul/emul_isl923x.h"
#include "test/drivers/charger_utils.h"
#include "test/drivers/test_state.h"

#include <zephyr/ztest.h>

#define CHARGER_NUM get_charger_num(&isl923x_drv)
#define ISL923X_EMUL EMUL_DT_GET(DT_NODELABEL(isl923x_emul))
#define COMMON_DATA emul_isl923x_get_i2c_common_data(ISL923X_EMUL)

ZTEST_SUITE(isl9238c, drivers_predicate_post_main, NULL, NULL, NULL, NULL);

#define SENSE_5MILLIOHM 5
/*
 * Verify the ISL9238C specific initialization paths.
 */
ZTEST(isl9238c, test_isl9238c_init)
{
	const struct emul *isl923x_emul = ISL923X_EMUL;
	int input_current;

	if (CONFIG_PLATFORM_EC_ISL9238C_INPUT_VOLTAGE_MV == -1) {
		isl923x_emul_reset_registers(isl923x_emul);
		i2c_common_emul_set_write_fail_reg(COMMON_DATA,
						   ISL9238_REG_INPUT_VOLTAGE);
		isl923x_drv.init(CHARGER_NUM);
		i2c_common_emul_set_write_fail_reg(COMMON_DATA,
						   I2C_COMMON_EMUL_NO_FAIL_REG);
		zassert_ok(isl923x_drv.get_input_current_limit(CHARGER_NUM,
							       &input_current),
			   NULL);
		zassert_equal(0, input_current,
			      "Expected input current 0mA but got %dmA",
			      input_current);
	}

	isl923x_emul_reset_registers(isl923x_emul);
	i2c_common_emul_set_read_fail_reg(COMMON_DATA, ISL9238C_REG_CONTROL6);
	isl923x_drv.init(CHARGER_NUM);
	i2c_common_emul_set_read_fail_reg(COMMON_DATA,
					  I2C_COMMON_EMUL_NO_FAIL_REG);
	zassert_ok(isl923x_drv.get_input_current_limit(CHARGER_NUM,
						       &input_current),
		   NULL);
	zassert_equal(0, input_current,
		      "Expected input current 0mA but got %dmA", input_current);

	isl923x_emul_reset_registers(isl923x_emul);
	i2c_common_emul_set_write_fail_reg(COMMON_DATA, ISL9238C_REG_CONTROL6);
	isl923x_drv.init(CHARGER_NUM);
	i2c_common_emul_set_write_fail_reg(COMMON_DATA,
					   I2C_COMMON_EMUL_NO_FAIL_REG);
	zassert_ok(isl923x_drv.get_input_current_limit(CHARGER_NUM,
						       &input_current),
		   NULL);
	zassert_equal(0, input_current,
		      "Expected input current 0mA but got %dmA", input_current);

	if (IS_ENABLED(CONFIG_PLATFORM_EC_ISL9238C_ENABLE_BUCK_MODE)) {
		uint32_t option;

		isl923x_emul_reset_registers(isl923x_emul);
		i2c_common_emul_set_read_fail_reg(COMMON_DATA,
						  ISL923X_REG_CONTROL0);
		isl923x_drv.init(CHARGER_NUM);
		i2c_common_emul_set_read_fail_reg(COMMON_DATA,
						  I2C_COMMON_EMUL_NO_FAIL_REG);
		zassert_ok(isl923x_drv.get_input_current_limit(CHARGER_NUM,
							       &input_current),
			   NULL);
		zassert_equal(0, input_current,
			      "Expected input current 0mA but got %dmA",
			      input_current);

		isl923x_emul_reset_registers(isl923x_emul);
		i2c_common_emul_set_write_fail_reg(COMMON_DATA,
						   ISL923X_REG_CONTROL0);
		isl923x_drv.init(CHARGER_NUM);
		i2c_common_emul_set_write_fail_reg(COMMON_DATA,
						   I2C_COMMON_EMUL_NO_FAIL_REG);
		zassert_ok(isl923x_drv.get_input_current_limit(CHARGER_NUM,
							       &input_current),
			   NULL);
		zassert_equal(0, input_current,
			      "Expected input current 0mA but got %dmA",
			      input_current);

		isl923x_emul_reset_registers(isl923x_emul);
		isl923x_drv.init(CHARGER_NUM);

		zassert_ok(isl923x_drv.get_option(CHARGER_NUM, &option));
		zassert_true(
			(option & ISL923X_C0_ENABLE_BUCK) != 0,
			"Expected options (0x%08x) to enable buck mode 0x%08x",
			option, ISL923X_C0_ENABLE_BUCK);
	}

	if (CONFIG_PLATFORM_EC_CHARGER_SENSE_RESISTOR == SENSE_5MILLIOHM) {
		isl923x_emul_reset_registers(isl923x_emul);
		i2c_common_emul_set_read_fail_reg(COMMON_DATA,
						  ISL923X_REG_CONTROL2);
		isl923x_drv.init(CHARGER_NUM);
		i2c_common_emul_set_read_fail_reg(COMMON_DATA,
						  I2C_COMMON_EMUL_NO_FAIL_REG);
		zassert_ok(isl923x_drv.get_input_current_limit(CHARGER_NUM,
							       &input_current),
			   NULL);
		zassert_equal(0, input_current,
			      "Expected input current 0mA but got %dmA",
			      input_current);

		isl923x_emul_reset_registers(isl923x_emul);
		i2c_common_emul_set_write_fail_reg(COMMON_DATA,
						   ISL923X_REG_CONTROL2);
		isl923x_drv.init(CHARGER_NUM);
		i2c_common_emul_set_write_fail_reg(COMMON_DATA,
						   I2C_COMMON_EMUL_NO_FAIL_REG);
		zassert_ok(isl923x_drv.get_input_current_limit(CHARGER_NUM,
							       &input_current),
			   NULL);
		zassert_equal(0, input_current,
			      "Expected input current 0mA but got %dmA",
			      input_current);

		isl923x_emul_reset_registers(isl923x_emul);
		i2c_common_emul_set_read_fail_reg(COMMON_DATA,
						  ISL9238_REG_CONTROL3);
		isl923x_drv.init(CHARGER_NUM);
		i2c_common_emul_set_read_fail_reg(COMMON_DATA,
						  I2C_COMMON_EMUL_NO_FAIL_REG);
		zassert_ok(isl923x_drv.get_input_current_limit(CHARGER_NUM,
							       &input_current),
			   NULL);
		zassert_equal(0, input_current,
			      "Expected input current 0mA but got %dmA",
			      input_current);

		isl923x_emul_reset_registers(isl923x_emul);
		i2c_common_emul_set_write_fail_reg(COMMON_DATA,
						   ISL9238_REG_CONTROL3);
		isl923x_drv.init(CHARGER_NUM);
		i2c_common_emul_set_write_fail_reg(COMMON_DATA,
						   I2C_COMMON_EMUL_NO_FAIL_REG);
		zassert_ok(isl923x_drv.get_input_current_limit(CHARGER_NUM,
							       &input_current),
			   NULL);
		zassert_equal(0, input_current,
			      "Expected input current 0mA but got %dmA",
			      input_current);
	}
}
