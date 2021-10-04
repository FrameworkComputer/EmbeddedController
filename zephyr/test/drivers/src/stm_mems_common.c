/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <ztest.h>
#include <device.h>
#include <devicetree.h>
#include <errno.h>

#include "common.h"
#include "driver/stm_mems_common.h"
#include "emul/emul_common_i2c.h"
#include "emul/i2c_mock.h"
#include "i2c/i2c.h"

#define MOCK_EMUL emul_get_binding(DT_LABEL(DT_NODELABEL(i2c_mock)))

struct mock_properties {
	/* Incremented by the mock function every time it is called */
	int call_count;
};

static void setup(void)
{
	i2c_mock_reset(MOCK_EMUL);
}

static int mock_read_fn(struct i2c_emul *emul, int reg, uint8_t *val, int bytes,
			void *data)
{
	ztest_check_expected_value(reg);
	ztest_check_expected_value(bytes);
	if (val != NULL) {
		/* Allow passing a mocked read byte through the output param */
		ztest_copy_return_data(val, sizeof(*val));
	}
	return ztest_get_return_value();
}

static int mock_write_fn(struct i2c_emul *emul, int reg, uint8_t val, int bytes,
			 void *data)
{
	struct mock_properties *props = (struct mock_properties *)data;

	if (props)
		props->call_count++;

	ztest_check_expected_value(reg);
	ztest_check_expected_value(val);
	ztest_check_expected_value(bytes);
	return ztest_get_return_value();
}

static void test_st_raw_read_n(void)
{
	const struct emul *emul = MOCK_EMUL;
	struct i2c_emul *i2c_emul = i2c_mock_to_i2c_emul(emul);
	int rv;

	i2c_common_emul_set_read_func(i2c_emul, mock_read_fn, NULL);
	/*
	 * Ensure the MSb (auto-increment bit) in the register address gets
	 * set, but also return an error condition
	 */
	ztest_expect_value(mock_read_fn, reg, 0x80);
	ztest_expect_value(mock_read_fn, bytes, 0);
	ztest_returns_value(mock_read_fn, -EIO);

	rv = st_raw_read_n(I2C_PORT_POWER, i2c_mock_get_addr(emul), 0, NULL, 2);
	/* The shim layer translates -EIO to EC_ERROR_INVAL. */
	zassert_equal(rv, EC_ERROR_INVAL, "rv was %d but expected %d", rv,
		      EC_ERROR_INVAL);
}

static void test_st_raw_read_n_noinc(void)
{
	const struct emul *emul = MOCK_EMUL;
	struct i2c_emul *i2c_emul = i2c_mock_to_i2c_emul(emul);
	int rv;

	i2c_common_emul_set_read_func(i2c_emul, mock_read_fn, NULL);
	/*
	 * Unlike `st_raw_read_n`, the MSb (auto-increment bit) in the register
	 * address should NOT be automatically set. Also return an error.
	 */
	ztest_expect_value(mock_read_fn, reg, 0x00);
	ztest_expect_value(mock_read_fn, bytes, 0);
	ztest_returns_value(mock_read_fn, -EIO);

	rv = st_raw_read_n_noinc(I2C_PORT_POWER, i2c_mock_get_addr(emul), 0,
				 NULL, 2);
	/* The shim layer translates -EIO to EC_ERROR_INVAL. */
	zassert_equal(rv, EC_ERROR_INVAL, "rv was %d but expected %d", rv,
		      EC_ERROR_INVAL);
}

static void test_st_write_data_with_mask(void)
{
	const struct emul *emul = MOCK_EMUL;
	struct i2c_emul *i2c_emul = i2c_mock_to_i2c_emul(emul);
	int rv;

	const struct motion_sensor_t sensor = {
		.port = I2C_PORT_POWER,
		.i2c_spi_addr_flags = i2c_mock_get_addr(emul),
	};

	/* Arbitrary named test parameters */
	uint8_t test_addr = 0xAA;
	uint8_t initial_value = 0x55;
	uint8_t test_mask = 0xF0;
	uint8_t test_data = 0xFF;
	uint8_t expected_new_value = (initial_value & ~test_mask) |
				     (test_data & test_mask);

	/* Part 1: error occurs when reading initial value from sensor */
	i2c_common_emul_set_read_func(i2c_emul, mock_read_fn, NULL);
	ztest_expect_value(mock_read_fn, reg, test_addr);
	ztest_expect_value(mock_read_fn, bytes, 0);
	/* Value is immaterial but ztest has no way to explicitly ignore it */
	ztest_return_data(mock_read_fn, val, &initial_value);
	ztest_returns_value(mock_read_fn, -EIO);

	rv = st_write_data_with_mask(&sensor, test_addr, test_mask, test_data);
	/* The shim layer translates -EIO to EC_ERROR_INVAL. */
	zassert_equal(rv, EC_ERROR_INVAL, "rv was %d but expected %d", rv,
		      EC_ERROR_INVAL);

	/*
	 * Part 2: initial read succeeds, but the initial value already
	 * matches the new value, so no write happens.
	 */
	ztest_expect_value(mock_read_fn, reg, test_addr);
	ztest_expect_value(mock_read_fn, bytes, 0);
	ztest_return_data(mock_read_fn, val, &expected_new_value);
	ztest_returns_value(mock_read_fn, 0);

	struct mock_properties write_fn_props = {
		.call_count = 0,
	};

	i2c_common_emul_set_write_func(i2c_emul, mock_write_fn,
				       &write_fn_props);

	rv = st_write_data_with_mask(&sensor, test_addr, test_mask, test_data);
	zassert_equal(rv, EC_SUCCESS, "rv was %d but expected %d", rv,
		      EC_SUCCESS);
	zassert_equal(write_fn_props.call_count, 0,
		      "mock_write_fn was called.");

	/*
	 * Part 3: this time a write is required, but it fails. This also
	 * tests the masking logic.
	 */
	ztest_expect_value(mock_read_fn, reg, test_addr);
	ztest_expect_value(mock_read_fn, bytes, 0);
	ztest_return_data(mock_read_fn, val, &initial_value);
	ztest_returns_value(mock_read_fn, 0);

	write_fn_props.call_count = 0; /* Reset call count */
	ztest_expect_value(mock_write_fn, reg, test_addr);
	ztest_expect_value(mock_write_fn, bytes, 1);
	ztest_expect_value(mock_write_fn, val, expected_new_value);
	ztest_returns_value(mock_write_fn, -EIO);

	rv = st_write_data_with_mask(&sensor, test_addr, test_mask, test_data);
	/* The shim layer translates -EIO to EC_ERROR_INVAL. */
	zassert_equal(rv, EC_ERROR_INVAL, "rv was %d but expected %d", rv,
		      EC_ERROR_INVAL);
	zassert_equal(write_fn_props.call_count, 1,
		      "mock_write_fn was not called.");
}

static void test_st_get_resolution(void)
{
	int expected_resolution = 123;
	int rv;

	struct stprivate_data driver_data = {
		.resol = expected_resolution,
	};

	const struct motion_sensor_t sensor = {
		.drv_data = &driver_data,
	};

	rv = st_get_resolution(&sensor);
	zassert_equal(rv, expected_resolution, "rv is %d but expected %d", rv,
		      expected_resolution);
}

static void test_st_get_data_rate(void)
{
	int expected_data_rate = 456;
	int rv;

	struct stprivate_data driver_data = {
		.base = {
			.odr = expected_data_rate,
		},
	};

	const struct motion_sensor_t sensor = {
		.drv_data = &driver_data,
	};

	rv = st_get_data_rate(&sensor);
	zassert_equal(rv, expected_data_rate, "rv is %d but expected %d", rv,
		      expected_data_rate);
}

void test_suite_stm_mems_common(void)
{
	ztest_test_suite(
		stm_mems_common,
		ztest_unit_test_setup_teardown(test_st_raw_read_n, setup,
					       unit_test_noop),
		ztest_unit_test_setup_teardown(test_st_raw_read_n_noinc, setup,
					       unit_test_noop),
		ztest_unit_test_setup_teardown(test_st_write_data_with_mask,
					       setup, unit_test_noop),
		ztest_unit_test(test_st_get_resolution),
		ztest_unit_test(test_st_get_data_rate));
	ztest_run_test_suite(stm_mems_common);
}
