/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <ztest.h>
#include <drivers/emul.h>
#include "driver/accel_lis2dw12.h"
#include "emul/emul_common_i2c.h"
#include "emul/emul_lis2dw12.h"

#define LIS2DW12_NODELABEL DT_NODELABEL(ms_lis2dw12_accel)
#define LIS2DW12_SENSOR_ID SENSOR_ID(LIS2DW12_NODELABEL)
#define EMUL_LABEL DT_LABEL(DT_NODELABEL(lis2dw12_emul))

#include <stdio.h>

enum lis2dw12_round_mode {
	ROUND_DOWN,
	ROUND_UP,
};

static void lis2dw12_setup(void)
{
	lis2dw12_emul_reset(emul_get_binding(EMUL_LABEL));

	/* Reset certain sensor struct values */
	struct motion_sensor_t *ms = &motion_sensors[LIS2DW12_SENSOR_ID];

	ms->current_range = 0;

}

static void test_lis2dw12_init__fail_read_who_am_i(void)
{
	const struct emul *emul = emul_get_binding(EMUL_LABEL);
	struct motion_sensor_t *ms = &motion_sensors[LIS2DW12_SENSOR_ID];
	int rv;

	i2c_common_emul_set_read_fail_reg(lis2dw12_emul_to_i2c_emul(emul),
					  LIS2DW12_WHO_AM_I_REG);
	rv = ms->drv->init(ms);
	zassert_equal(EC_ERROR_INVAL, rv, NULL);
}

static void test_lis2dw12_init__fail_who_am_i(void)
{
	const struct emul *emul = emul_get_binding(EMUL_LABEL);
	struct motion_sensor_t *ms = &motion_sensors[LIS2DW12_SENSOR_ID];
	int rv;

	lis2dw12_emul_set_who_am_i(emul, ~LIS2DW12_WHO_AM_I);

	rv = ms->drv->init(ms);
	zassert_equal(EC_ERROR_ACCESS_DENIED, rv,
		      "init returned %d but was expecting %d", rv,
		      EC_ERROR_ACCESS_DENIED);
}

static void test_lis2dw12_init__fail_write_soft_reset(void)
{
	const struct emul *emul = emul_get_binding(EMUL_LABEL);
	struct motion_sensor_t *ms = &motion_sensors[LIS2DW12_SENSOR_ID];
	int rv;

	i2c_common_emul_set_write_fail_reg(lis2dw12_emul_to_i2c_emul(emul),
					   LIS2DW12_SOFT_RESET_ADDR);
	rv = ms->drv->init(ms);
	zassert_equal(EC_ERROR_INVAL, rv, NULL);
}

static void test_lis2dw12_init__timeout_read_soft_reset(void)
{
	const struct emul *emul = emul_get_binding(EMUL_LABEL);
	struct motion_sensor_t *ms = &motion_sensors[LIS2DW12_SENSOR_ID];
	int rv;

	i2c_common_emul_set_read_fail_reg(lis2dw12_emul_to_i2c_emul(emul),
					  LIS2DW12_SOFT_RESET_ADDR);
	rv = ms->drv->init(ms);
	zassert_equal(EC_ERROR_TIMEOUT, rv, "init returned %d but expected %d",
		      rv, EC_ERROR_TIMEOUT);
}

static int lis2dw12_test_mock_write_fail_set_bdu(struct i2c_emul *emul, int reg,
						uint8_t val, int bytes,
						void *data)
{
	if (reg == LIS2DW12_BDU_ADDR && bytes == 1 &&
	    (val & LIS2DW12_BDU_MASK) != 0) {
		return -EIO;
	}
	return 1;
}

static void test_lis2dw12_init__fail_set_bdu(void)
{
	const struct emul *emul = emul_get_binding(EMUL_LABEL);
	struct motion_sensor_t *ms = &motion_sensors[LIS2DW12_SENSOR_ID];
	int rv;

	i2c_common_emul_set_write_func(lis2dw12_emul_to_i2c_emul(emul),
				      lis2dw12_test_mock_write_fail_set_bdu,
				      NULL);
	rv = ms->drv->init(ms);
	zassert_equal(EC_ERROR_INVAL, rv, "init returned %d but expected %d",
		      rv, EC_ERROR_INVAL);
	zassert_true(lis2dw12_emul_get_soft_reset_count(emul) > 0,
		      "expected at least one soft reset");
}

static void test_lis2dw12_set_power_mode(void)
{
	const struct emul *emul = emul_get_binding(EMUL_LABEL);
	struct motion_sensor_t *ms = &motion_sensors[LIS2DW12_SENSOR_ID];
	int rv;

	/* Part 1: happy path */
	rv = lis2dw12_set_power_mode(ms, LIS2DW12_LOW_POWER,
				     LIS2DW12_LOW_POWER_MODE_2);
	zassert_equal(rv, EC_SUCCESS, "Expected %d but got %d", EC_SUCCESS, rv);

	/* Part 2: unimplemented modes */
	rv = lis2dw12_set_power_mode(ms, LIS2DW12_LOW_POWER,
				     LIS2DW12_LOW_POWER_MODE_1);
	zassert_equal(rv, EC_ERROR_UNIMPLEMENTED, "Expected %d but got %d",
		      EC_ERROR_UNIMPLEMENTED, rv);

	/* Part 3: attempt to set mode but cannot modify reg. */
	i2c_common_emul_set_read_fail_reg(lis2dw12_emul_to_i2c_emul(emul),
					  LIS2DW12_ACC_MODE_ADDR);
	rv = lis2dw12_set_power_mode(ms, LIS2DW12_LOW_POWER,
				     LIS2DW12_LOW_POWER_MODE_2);
	zassert_equal(rv, EC_ERROR_INVAL, "Expected %d but got %d",
		      EC_ERROR_INVAL, rv);
}

static void test_lis2dw12_set_range(void)
{
	const struct emul *emul = emul_get_binding(EMUL_LABEL);
	struct motion_sensor_t *ms = &motion_sensors[LIS2DW12_SENSOR_ID];
	int rv;

	/* Part 1: Happy path. Go above the max range; it will be automatically
	 * clamped.
	 */

	rv = ms->drv->set_range(ms, LIS2DW12_ACCEL_FS_MAX_VAL + 1, 0);
	zassert_equal(rv, EC_SUCCESS, "Expected %d but got %d", EC_SUCCESS, rv);
	zassert_equal(ms->current_range, LIS2DW12_ACCEL_FS_MAX_VAL,
		      "Expected %d but got %d", LIS2DW12_ACCEL_FS_MAX_VAL,
		      ms->current_range);

	/* Part 2: Error accessing register */
	i2c_common_emul_set_read_fail_reg(lis2dw12_emul_to_i2c_emul(emul),
					  LIS2DW12_FS_ADDR);
	rv = ms->drv->set_range(ms, LIS2DW12_ACCEL_FS_MAX_VAL, 0);
	zassert_equal(rv, EC_ERROR_INVAL, "Expected %d but got %d",
		      EC_ERROR_INVAL, rv);
}

static void test_lis2dw12_set_rate(void)
{
	const struct emul *emul = emul_get_binding(EMUL_LABEL);
	struct i2c_emul *i2c_emul = lis2dw12_emul_to_i2c_emul(emul);
	struct motion_sensor_t *ms = &motion_sensors[LIS2DW12_SENSOR_ID];
	struct stprivate_data *drv_data = ms->drv_data;
	int rv;

	/* Part 1: Turn off sensor with rate=0 */
	rv = ms->drv->set_data_rate(ms, 0, 0);

	zassert_equal(lis2dw12_emul_peek_odr(i2c_emul),
		      LIS2DW12_ODR_POWER_OFF_VAL,
		      "Output data rate should be %d but got %d",
		      LIS2DW12_ODR_POWER_OFF_VAL,
		      lis2dw12_emul_peek_odr(i2c_emul));
	zassert_equal(drv_data->base.odr, LIS2DW12_ODR_POWER_OFF_VAL,
		      "Output data rate should be %d but got %d",
		      LIS2DW12_ODR_POWER_OFF_VAL, drv_data->base.odr);
	zassert_equal(rv, EC_SUCCESS, "Returned %d but expected %d", rv,
		      EC_SUCCESS);

	/* Part 2: Set some output data rates. We will request a certain rate
	 * and make sure the closest supported rate is used.
	 */

	static const struct {
		int requested_rate; /* millihertz */
		enum lis2dw12_round_mode round;
		int expected_norm_rate; /* millihertz */
		uint8_t expected_reg_val;
	} test_params[] = {
		{ 1000, ROUND_DOWN, LIS2DW12_ODR_MIN_VAL,
		  LIS2DW12_ODR_12HZ_VAL },
		{ 12501, ROUND_DOWN, 12500, LIS2DW12_ODR_12HZ_VAL },
		{ 25001, ROUND_DOWN, 25000, LIS2DW12_ODR_25HZ_VAL },
		{ 50001, ROUND_DOWN, 50000, LIS2DW12_ODR_50HZ_VAL },
		{ 100001, ROUND_DOWN, 100000, LIS2DW12_ODR_100HZ_VAL },
		{ 200001, ROUND_DOWN, 200000, LIS2DW12_ODR_200HZ_VAL },
		{ 400001, ROUND_DOWN, 400000, LIS2DW12_ODR_400HZ_VAL },
		{ 800001, ROUND_DOWN, 800000, LIS2DW12_ODR_800HZ_VAL },
		{ 1600001, ROUND_DOWN, 1600000, LIS2DW12_ODR_1_6kHZ_VAL },
		{ 3200001, ROUND_DOWN, LIS2DW12_ODR_MAX_VAL,
		  LIS2DW12_ODR_1_6kHZ_VAL },

		{ 1000, ROUND_UP, LIS2DW12_ODR_MIN_VAL, LIS2DW12_ODR_12HZ_VAL },
		{ 12501, ROUND_UP, 25000, LIS2DW12_ODR_25HZ_VAL },
		{ 25001, ROUND_UP, 50000, LIS2DW12_ODR_50HZ_VAL },
		{ 50001, ROUND_UP, 100000, LIS2DW12_ODR_100HZ_VAL },
		{ 100001, ROUND_UP, 200000, LIS2DW12_ODR_200HZ_VAL },
		{ 200001, ROUND_UP, 400000, LIS2DW12_ODR_400HZ_VAL },
		{ 400001, ROUND_UP, 800000, LIS2DW12_ODR_800HZ_VAL },
		{ 800001, ROUND_UP, 1600000, LIS2DW12_ODR_1_6kHZ_VAL },
		{ 1600001, ROUND_UP, LIS2DW12_ODR_MAX_VAL,
		  LIS2DW12_ODR_1_6kHZ_VAL },
	};

	for (size_t i = 0; i < ARRAY_SIZE(test_params); i++) {
		/* For each test vector in the above array */
		drv_data->base.odr = -1;
		rv = ms->drv->set_data_rate(ms, test_params[i].requested_rate,
					    test_params[i].round);

		/* Check the normalized rate the driver chose */
		zassert_equal(
			drv_data->base.odr, test_params[i].expected_norm_rate,
			"For requested rate %d, output data rate should be %d but got %d",
			test_params[i].requested_rate,
			test_params[i].expected_norm_rate, drv_data->base.odr);

		/* Read ODR and mode bits back from CTRL1 register */
		uint8_t odr_bits = lis2dw12_emul_peek_odr(i2c_emul);

		zassert_equal(
			odr_bits, test_params[i].expected_reg_val,
			"For requested rate %d, ODR bits should be 0x%x but got 0x%x - %d",
			test_params[i].requested_rate,
			test_params[i].expected_reg_val, odr_bits,
			LIS2DW12_ODR_MAX_VAL);

		/* Check if high performance mode was enabled if rate >
		 * 200,000mHz
		 */

		uint8_t mode_bits = lis2dw12_emul_peek_mode(i2c_emul);
		uint8_t lpmode_bits = lis2dw12_emul_peek_lpmode(i2c_emul);

		if (odr_bits > LIS2DW12_ODR_200HZ_VAL) {
			/* High performance mode, LP mode immaterial */
			zassert_equal(mode_bits, LIS2DW12_HIGH_PERF,
				      "MODE[1:0] should be 0x%x, but got 0x%x",
				      LIS2DW12_HIGH_PERF, mode_bits);

		} else {
			/* Low power mode, LP mode 2 */
			zassert_equal(mode_bits, LIS2DW12_LOW_POWER,
				      "MODE[1:0] should be 0x%x, but got 0x%x",
				      LIS2DW12_LOW_POWER, mode_bits);

			zassert_equal(
				lpmode_bits, LIS2DW12_LOW_POWER_MODE_2,
				"LPMODE[1:0] should be 0x%x, but got 0x%x",
				LIS2DW12_LOW_POWER_MODE_2, lpmode_bits);
		}
	}
}

void test_suite_lis2dw12(void)
{
	ztest_test_suite(lis2dw12,
			 ztest_unit_test_setup_teardown(
				 test_lis2dw12_init__fail_read_who_am_i,
				 lis2dw12_setup, lis2dw12_setup),
			 ztest_unit_test_setup_teardown(
				 test_lis2dw12_init__fail_who_am_i,
				 lis2dw12_setup, lis2dw12_setup),
			 ztest_unit_test_setup_teardown(
				 test_lis2dw12_init__fail_write_soft_reset,
				 lis2dw12_setup, lis2dw12_setup),
			 ztest_unit_test_setup_teardown(
				 test_lis2dw12_init__timeout_read_soft_reset,
				 lis2dw12_setup, lis2dw12_setup),
			 ztest_unit_test_setup_teardown(
				 test_lis2dw12_init__fail_set_bdu,
				 lis2dw12_setup, lis2dw12_setup),
			 ztest_unit_test_setup_teardown(
				 test_lis2dw12_set_power_mode,
				 lis2dw12_setup, lis2dw12_setup),
			 ztest_unit_test_setup_teardown(
				 test_lis2dw12_set_range,
				 lis2dw12_setup, lis2dw12_setup),
			 ztest_unit_test_setup_teardown(
				 test_lis2dw12_set_rate,
				 lis2dw12_setup, lis2dw12_setup)
			 );
	ztest_run_test_suite(lis2dw12);
}
