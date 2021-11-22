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
static void lis2dw12_setup(void)
{
	lis2dw12_emul_reset(emul_get_binding(EMUL_LABEL));
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
				 lis2dw12_setup, lis2dw12_setup));
	ztest_run_test_suite(lis2dw12);
}
