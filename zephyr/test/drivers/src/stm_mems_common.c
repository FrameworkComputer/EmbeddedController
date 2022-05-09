/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <ztest.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <errno.h>

#include "common.h"
#include "driver/stm_mems_common.h"
#include "emul/emul_common_i2c.h"
#include "emul/i2c_mock.h"
#include "i2c/i2c.h"
#include "test/drivers/test_state.h"

#define MOCK_EMUL emul_get_binding(DT_LABEL(DT_NODELABEL(i2c_mock)))

struct mock_properties {
	/* Incremented by the mock function every time it is called */
	int call_count;
};

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

ZTEST(stm_mems_common, test_st_raw_read_n)
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

ZTEST(stm_mems_common, test_st_raw_read_n_noinc)
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

ZTEST(stm_mems_common, test_st_write_data_with_mask)
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

ZTEST(stm_mems_common, test_st_get_resolution)
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

ZTEST(stm_mems_common, test_st_set_offset)
{
	int16_t expected_offset[3] = { 123, 456, 789 };

	struct stprivate_data driver_data;
	const struct motion_sensor_t sensor = {
		.drv_data = &driver_data,
	};

	int rv = st_set_offset(&sensor, expected_offset, 0);

	zassert_equal(rv, EC_SUCCESS, "rv as %d but expected %d", rv,
		      EC_SUCCESS);
	zassert_equal(driver_data.offset[X], expected_offset[X],
		      "X offset is %d but expected %d", driver_data.offset[X],
		      expected_offset[X]);
	zassert_equal(driver_data.offset[Y], expected_offset[Y],
		      "Y offset is %d but expected %d", driver_data.offset[Y],
		      expected_offset[Y]);
	zassert_equal(driver_data.offset[Z], expected_offset[Z],
		      "Z offset is %d but expected %d", driver_data.offset[Z],
		      expected_offset[Z]);
}

ZTEST(stm_mems_common, test_st_get_offset)
{
	struct stprivate_data driver_data = {
		.offset = { [X] = 123, [Y] = 456, [Z] = 789 },
	};
	const struct motion_sensor_t sensor = {
		.drv_data = &driver_data,
	};

	int16_t temp_out = 0;
	int16_t actual_offset[3] = { 0, 0, 0 };

	int rv = st_get_offset(&sensor, actual_offset, &temp_out);

	zassert_equal(rv, EC_SUCCESS, "rv as %d but expected %d", rv,
		      EC_SUCCESS);
	zassert_equal(
		temp_out, (int16_t)EC_MOTION_SENSE_INVALID_CALIB_TEMP,
		"temp is %d but should be %d (EC_MOTION_SENSE_INVALID_CALIB_TEMP)",
		temp_out, (int16_t)EC_MOTION_SENSE_INVALID_CALIB_TEMP);

	zassert_equal(actual_offset[X], driver_data.offset[X],
		      "X offset is %d but expected %d", actual_offset[X],
		      driver_data.offset[X]);
	zassert_equal(actual_offset[Y], driver_data.offset[Y],
		      "Y offset is %d but expected %d", actual_offset[Y],
		      driver_data.offset[Y]);
	zassert_equal(actual_offset[Z], driver_data.offset[Z],
		      "Z offset is %d but expected %d", actual_offset[Z],
		      driver_data.offset[Z]);
}

ZTEST(stm_mems_common, test_st_get_data_rate)
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

ZTEST(stm_mems_common, test_st_normalize)
{
	struct stprivate_data driver_data = {
		.resol = 12, /* 12 bits of useful data (arbitrary) */
		.offset = {  /* Arbitrary offsets */
			[X] = -100,
			[Y] = 200,
			[Z] = 100,
		},
	};
	/* Fixed-point identity matrix that performs no rotation. */
	const mat33_fp_t identity_rot_matrix = {
		{ INT_TO_FP(1), INT_TO_FP(0), INT_TO_FP(0) },
		{ INT_TO_FP(0), INT_TO_FP(1), INT_TO_FP(0) },
		{ INT_TO_FP(0), INT_TO_FP(0), INT_TO_FP(1) },
	};
	const struct motion_sensor_t sensor = {
		.drv_data = &driver_data,
		.rot_standard_ref = &identity_rot_matrix,
		.current_range = 32, /* used to scale offsets (arbitrary) */
	};

	/* Accelerometer data is passed in with the format:
	 * (lower address)                  (higher address)
	 *  [X LSB] [X MSB] [Y LSB] [Y MSB] [Z LSB] [Z MSB]
	 *
	 * The LSB are left-aligned and contain noise/junk data
	 * in their least-significant bit positions. When interpreted
	 * as int16 samples, the `driver_data.resol`-count most
	 * significant bits are what we actually use. For this test, we
	 * set `resol` to 12, so there will be 12 useful bits and 4 noise
	 * bits. This test (and the EC code) assume we are compiling on
	 * a little-endian machine. The samples themselvesare unsigned and
	 * biased at 2^12/2 = 2^11.
	 */
	uint16_t input_reading[] = {
		((BIT(11) - 100) << 4) | 0x000a,
		((BIT(11) + 0) << 4) | 0x000b,
		((BIT(11) + 100) << 4) | 0x000c,
	};

	/* Expected outputs w/ noise bits suppressed and offsets applied.
	 * Note that the data stays left-aligned.
	 */
	intv3_t expected_output = {
		((BIT(11) - 100) << 4) + driver_data.offset[X],
		((BIT(11) + 0) << 4) + driver_data.offset[Y],
		((BIT(11) + 100) << 4) + driver_data.offset[Z],
	};

	intv3_t actual_output = { 0 };

	st_normalize(&sensor, (int *)&actual_output, (uint8_t *)input_reading);

	zassert_within(actual_output[X], expected_output[X], 0.5f,
		      "X output is %d but expected %d", actual_output[X],
		      expected_output[X]);
	zassert_within(actual_output[Y], expected_output[Y], 0.5f,
		      "Y output is %d but expected %d", actual_output[Y],
		      expected_output[Y]);
	zassert_within(actual_output[Z], expected_output[Z], 0.5f,
		      "Z output is %d but expected %d", actual_output[Z],
		      expected_output[Z]);
}

static void stm_mems_common_before(void *state)
{
	ARG_UNUSED(state);
	i2c_mock_reset(MOCK_EMUL);
}

ZTEST_SUITE(stm_mems_common, drivers_predicate_post_main, NULL,
	    stm_mems_common_before, NULL, NULL);
