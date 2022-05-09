/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <ztest.h>

static void test_i2c_get_device(void)
{
	const struct device *accel0 = DEVICE_DT_GET(
			DT_PHANDLE(DT_PATH(named_i2c_ports, accel_0),
				   i2c_port));
	const struct device *bmi_i2c = DEVICE_DT_GET(
			DT_NODELABEL(bmi_i2c));

	zassert_not_null(accel0, "accel0 was NULL");
	zassert_not_null(bmi_i2c, "bmi_i2c was NULL");
	zassert_equal(accel0, bmi_i2c,
		"named_i2c_ports/accel0 and bmi_i2c should resolve to the same device");
}

/* test case main entry */
void test_main(void)
{
	ztest_test_suite(test_i2c_bindings,
			 ztest_user_unit_test(test_i2c_get_device));
	ztest_run_test_suite(test_i2c_bindings);
}
