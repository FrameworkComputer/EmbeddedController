/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <device.h>
#include <ztest.h>

#include "drivers/cros_cbi.h"

static void test_check_match(void)
{
	const struct device *dev = device_get_binding(CROS_CBI_LABEL);
	int value;

	zassert_not_null(dev, NULL);

	value = cros_cbi_ssfc_check_match(
		dev, CBI_SSFC_VALUE_ID(DT_NODELABEL(base_sensor_0)));
	zassert_true(value, "Expected cbi ssfc to match base_sensor_0");

	value = cros_cbi_ssfc_check_match(
		dev, CBI_SSFC_VALUE_ID(DT_NODELABEL(base_sensor_1)));
	zassert_false(value, "Expected cbi ssfc not to match base_sensor_1");

	value = cros_cbi_ssfc_check_match(dev, CBI_SSFC_VALUE_COUNT);
	zassert_false(value, "Expected cbi ssfc to fail on invalid enum");
}

void test_suite_cros_cbi(void)
{
	ztest_test_suite(cros_cbi, ztest_unit_test(test_check_match));
	ztest_run_test_suite(cros_cbi);
}
