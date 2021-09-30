/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <ztest.h>
#include <drivers/emul.h>

#include "battery.h"
#include "charger_utils.h"
#include "driver/charger/isl923x.h"
#include "driver/charger/isl923x_public.h"
#include "emul/emul_common_i2c.h"
#include "emul/emul_isl923x.h"

BUILD_ASSERT(CONFIG_CHARGER_SENSE_RESISTOR == 10 ||
	     CONFIG_CHARGER_SENSE_RESISTOR == 5);

BUILD_ASSERT(CONFIG_CHARGER_SENSE_RESISTOR_AC == 20 ||
	     CONFIG_CHARGER_SENSE_RESISTOR_AC == 10);

#if CONFIG_CHARGER_SENSE_RESISTOR == 10
#define EXPECTED_CURRENT_MA(n) (n)
#else
#define EXPECTED_CURRENT_MA(n) (n * 2)
#endif

#if CONFIG_CHARGER_SENSE_RESISTOR_AC == 20
#define EXPECTED_INPUT_CURRENT_MA(n) (n)
#else
#define EXPECTED_INPUT_CURRENT_MA(n) (n * 2)
#endif

#define CHARGER_NUM get_charger_num(&isl923x_drv)
#define ISL923X_EMUL emul_get_binding(DT_LABEL(DT_NODELABEL(isl923x_emul)))

void test_isl923x_set_current(void)
{
	int expected_current_milli_amps[] = {
		EXPECTED_CURRENT_MA(0),	   EXPECTED_CURRENT_MA(4),
		EXPECTED_CURRENT_MA(8),	   EXPECTED_CURRENT_MA(16),
		EXPECTED_CURRENT_MA(32),   EXPECTED_CURRENT_MA(64),
		EXPECTED_CURRENT_MA(128),  EXPECTED_CURRENT_MA(256),
		EXPECTED_CURRENT_MA(512),  EXPECTED_CURRENT_MA(1024),
		EXPECTED_CURRENT_MA(2048), EXPECTED_CURRENT_MA(4096)
	};
	int current_mA;

	for (int i = 0; i < ARRAY_SIZE(expected_current_milli_amps); ++i) {
		zassert_ok(isl923x_drv.set_current(
				   CHARGER_NUM, expected_current_milli_amps[i]),
			   "Failed to set the current to %dmA",
			   expected_current_milli_amps[i]);
		zassert_ok(isl923x_drv.get_current(CHARGER_NUM, &current_mA),
			   "Failed to get current");
		zassert_equal(expected_current_milli_amps[i], current_mA,
			      "Expected current %dmA but got %dmA",
			      expected_current_milli_amps[i], current_mA);
	}
}

void test_isl923x_set_voltage(void)
{
	int expected_voltage_milli_volts[] = { 8,    16,   32,	 64,
					       128,  256,  512,	 1024,
					       2048, 4096, 8192, 16384 };
	int voltage_milli_volts;

	/* Test 0mV first, it's a special case because of voltage_min */
	zassert_ok(isl923x_drv.set_voltage(CHARGER_NUM, 0),
		   "Failed to set the voltage to 0mV");
	zassert_ok(isl923x_drv.get_voltage(CHARGER_NUM, &voltage_milli_volts),
		   "Failed to get voltage");
	zassert_equal(battery_get_info()->voltage_min, voltage_milli_volts,
		      "Expected voltage %dmV but got %dmV",
		      battery_get_info()->voltage_min, voltage_milli_volts);

	for (int i = 0; i < ARRAY_SIZE(expected_voltage_milli_volts); ++i) {
		zassert_ok(isl923x_drv.set_voltage(
				   CHARGER_NUM,
				   expected_voltage_milli_volts[i]),
			   "Failed to set the voltage to %dmV",
			   expected_voltage_milli_volts[i]);
		zassert_ok(isl923x_drv.get_voltage(CHARGER_NUM,
						   &voltage_milli_volts),
			   "Failed to get voltage");
		zassert_equal(expected_voltage_milli_volts[i],
			      voltage_milli_volts,
			      "Expected voltage %dmV but got %dmV",
			      expected_voltage_milli_volts[i],
			      voltage_milli_volts);
	}
}

void test_isl923x_set_input_current_limit(void)
{
	const struct emul *isl923x_emul = ISL923X_EMUL;
	struct i2c_emul *i2c_emul = isl923x_emul_get_i2c_emul(isl923x_emul);
	int expected_current_milli_amps[] = {
		EXPECTED_INPUT_CURRENT_MA(0),
		EXPECTED_INPUT_CURRENT_MA(4),
		EXPECTED_INPUT_CURRENT_MA(8),
		EXPECTED_INPUT_CURRENT_MA(16),
		EXPECTED_INPUT_CURRENT_MA(32),
		EXPECTED_INPUT_CURRENT_MA(64),
		EXPECTED_INPUT_CURRENT_MA(128),
		EXPECTED_INPUT_CURRENT_MA(256),
		EXPECTED_INPUT_CURRENT_MA(512),
		EXPECTED_INPUT_CURRENT_MA(1024),
		EXPECTED_INPUT_CURRENT_MA(2048),
		EXPECTED_INPUT_CURRENT_MA(4096) };
	int current_milli_amps;

	/* Test failing to write to current limit 1 reg */
	i2c_common_emul_set_write_fail_reg(i2c_emul,
					   ISL923X_REG_ADAPTER_CURRENT_LIMIT1);
	zassert_equal(EC_ERROR_INVAL,
		      isl923x_drv.set_input_current_limit(CHARGER_NUM, 0),
		      NULL);

	/* Test failing to write to current limit 2 reg */
	i2c_common_emul_set_write_fail_reg(i2c_emul,
					   ISL923X_REG_ADAPTER_CURRENT_LIMIT2);
	zassert_equal(EC_ERROR_INVAL,
		      isl923x_drv.set_input_current_limit(CHARGER_NUM, 0),
		      NULL);

	/* Reset fail register */
	i2c_common_emul_set_write_fail_reg(i2c_emul,
					   I2C_COMMON_EMUL_NO_FAIL_REG);

	/* Test failing to read current limit 1 reg */
	i2c_common_emul_set_read_fail_reg(i2c_emul,
					  ISL923X_REG_ADAPTER_CURRENT_LIMIT1);
	zassert_equal(EC_ERROR_INVAL,
		      isl923x_drv.get_input_current_limit(CHARGER_NUM,
							  &current_milli_amps),
		      NULL);

	/* Reset fail register */
	i2c_common_emul_set_read_fail_reg(i2c_emul,
					  I2C_COMMON_EMUL_NO_FAIL_REG);

	/* Test normal code path */
	for (int i = 0; i < ARRAY_SIZE(expected_current_milli_amps); ++i) {
		zassert_ok(isl923x_drv.set_input_current_limit(
				   CHARGER_NUM, expected_current_milli_amps[i]),
			   "Failed to set input current limit to %dmV",
			   expected_current_milli_amps[i]);
		zassert_ok(isl923x_drv.get_input_current_limit(
				   CHARGER_NUM, &current_milli_amps),
			   "Failed to get input current limit");
		zassert_equal(expected_current_milli_amps[i],
			      current_milli_amps,
			      "Expected input current %dmA but got %dmA",
			      expected_current_milli_amps[i],
			      current_milli_amps);
	}
}

void test_suite_isl923x(void)
{
	ztest_test_suite(isl923x,
			 ztest_unit_test(test_isl923x_set_current),
			 ztest_unit_test(test_isl923x_set_voltage),
			 ztest_unit_test(test_isl923x_set_input_current_limit));
	ztest_run_test_suite(isl923x);
}
