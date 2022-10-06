/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/ztest.h>

ZTEST(i2c_bindings, test_i2c_get_device)
{
	const struct device *accel0 = DEVICE_DT_GET(
		DT_PHANDLE(DT_PATH(named_i2c_ports, accel_0), i2c_port));
	const struct device *bmi_i2c = DEVICE_DT_GET(DT_NODELABEL(i2c0));

	zassert_not_null(accel0, "accel0 was NULL");
	zassert_not_null(bmi_i2c, "bmi_i2c was NULL");
	zassert_equal(
		accel0, bmi_i2c,
		"named_i2c_ports/accel0 and bmi_i2c should resolve to the same device");
}

ZTEST_SUITE(i2c_bindings, NULL, NULL, NULL, NULL, NULL);
