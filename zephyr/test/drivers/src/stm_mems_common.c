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

static void setup(void)
{
	i2c_mock_reset(MOCK_EMUL);
}

static const struct device *get_i2c_dev(void)
{
	const struct device *i2c_dev =
		device_get_binding(DT_LABEL(DT_NODELABEL(i2c0)));

	zassert_not_null(i2c_dev, "Failed to find 'i2c0' node");
	zassert_equal(i2c_dev, i2c_get_device_for_port(I2C_PORT_POWER),
		      "i2c0 node doesn't match I2C_PORT_POWER");
	return i2c_dev;
}

static int mock_read_fn(struct i2c_emul *emul, int reg, uint8_t *val, int bytes,
			void *data)
{
	ztest_check_expected_value(reg);
	ztest_check_expected_value(bytes);
	return ztest_get_return_value();
}

static void test_st_raw_read_n(void)
{
	const struct emul *emul = MOCK_EMUL;
	struct i2c_emul *i2c_emul = i2c_mock_to_i2c_emul(emul);
	int rv;

	i2c_common_emul_set_read_func(i2c_emul, mock_read_fn, NULL);
	ztest_expect_value(mock_read_fn, reg, 0x80);
	ztest_expect_value(mock_read_fn, bytes, 0);
	ztest_returns_value(mock_read_fn, -EIO);

	rv = st_raw_read_n(I2C_PORT_POWER, i2c_mock_get_addr(emul), 0, NULL, 2);
	/* The shim layer translates -EIO to EC_ERROR_INVAL. */
	zassert_equal(rv, EC_ERROR_INVAL, "rv was %d but expected %d", rv,
		      EC_ERROR_INVAL);
}

void test_suite_stm_mems_common(void)
{
	ztest_test_suite(stm_mems_common,
			 ztest_unit_test_setup_teardown(
				 test_st_raw_read_n,
				 unit_test_noop, unit_test_noop));
	ztest_run_test_suite(stm_mems_common);
}
