/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cros_cbi.h"
#include "test/drivers/test_state.h"

#include <zephyr/device.h>
#include <zephyr/ztest.h>

ZTEST(cros_cbi, test_check_match)
{
	int value;

	value = cros_cbi_ssfc_check_match(
		CBI_SSFC_VALUE_ID(DT_NODELABEL(base_sensor_0)));
	zassert_true(value, "Expected cbi ssfc to match base_sensor_0");

	value = cros_cbi_ssfc_check_match(
		CBI_SSFC_VALUE_ID(DT_NODELABEL(base_sensor_1)));
	zassert_false(value, "Expected cbi ssfc not to match base_sensor_1");

	value = cros_cbi_ssfc_check_match(CBI_SSFC_VALUE_COUNT);
	zassert_false(value, "Expected cbi ssfc to fail on invalid enum");
}

ZTEST(cros_cbi, test_fail_check_match)
{
	int value;

	value = cros_cbi_ssfc_check_match(CBI_SSFC_VALUE_COUNT);
	zassert_false(value,
		      "Expected cbi ssfc to never match CBI_SSFC_VALUE_COUNT");
}

ZTEST(cros_cbi, test_fw_config)
{
	uint32_t value;
	int ret;

	ret = cros_cbi_get_fw_config(FW_CONFIG_FIELD_1, &value);
	zassert_true(ret == 0,
		     "Expected no error return from cros_cbi_get_fw_config");
	zassert_true(value == FW_FIELD_1_B,
		     "Expected field value to match FW_FIELD_1_B as default");

	ret = cros_cbi_get_fw_config(FW_CONFIG_FIELD_2, &value);
	zassert_true(ret == 0,
		     "Expected no error return from cros_cbi_get_fw_config");
	zassert_false(value == FW_FIELD_2_X,
		      "Expected field value to not match FW_FIELD_2_X");

	ret = cros_cbi_get_fw_config(CBI_FW_CONFIG_FIELDS_COUNT, &value);
	zassert_equal(ret, -EINVAL,
		      "Expected error return from cros_cbi_get_fw_config");
}

ZTEST_SUITE(cros_cbi, drivers_predicate_post_main, NULL, NULL, NULL, NULL);
