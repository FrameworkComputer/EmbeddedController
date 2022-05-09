/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/devicetree.h>
#include <ztest.h>

#include "common.h"
#include "i2c/i2c.h"

/* Unused: required for shimming i2c. */
void watchdog_reload(void)
{
}

static void test_i2c_port_count(void)
{
	zassert_equal(NAMED_I2C(accel_0), 0,
		      "accel_0 expected to be 0 but was %d",
		      NAMED_I2C(accel_0));
	zassert_equal(I2C_PORT_COUNT, 1,
		      "I2C_PORT_COUNT expected to be 1 but was %d",
		      I2C_PORT_COUNT);
}

/* Test case main entry. */
void test_main(void)
{
	ztest_test_suite(test_i2c,
			 ztest_user_unit_test(test_i2c_port_count));
	ztest_run_test_suite(test_i2c);
}
