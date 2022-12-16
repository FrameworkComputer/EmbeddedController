/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cros_cbi.h"
#include "fan.h"
#include "hooks.h"

#include <zephyr/fff.h>
#include <zephyr/ztest.h>

DECLARE_FAKE_VALUE_FUNC(int, cros_cbi_get_fw_config,
			enum cbi_fw_config_field_id, uint32_t *);
DEFINE_FAKE_VALUE_FUNC(int, cros_cbi_get_fw_config, enum cbi_fw_config_field_id,
		       uint32_t *);
DECLARE_FAKE_VOID_FUNC(fan_set_count, int);
DEFINE_FAKE_VOID_FUNC(fan_set_count, int);

int cros_cbi_get_fw_config_mock(enum cbi_fw_config_field_id field_id,
				uint32_t *value)
{
	*value = FW_FAN_PRESENT;
	return 0;
}

int cros_cbi_get_fw_config_mock_no_fan(enum cbi_fw_config_field_id field_id,
				       uint32_t *value)
{
	*value = FW_FAN_NOT_PRESENT;
	return 0;
}

int cros_cbi_get_fw_config_mock_error(enum cbi_fw_config_field_id field_id,
				      uint32_t *value)
{
	return -1;
}

void fan_set_count_mock(int count)
{
	zassert_equal(0, count);
}

static void fan_before(void *fixture)
{
	ARG_UNUSED(fixture);

	RESET_FAKE(cros_cbi_get_fw_config);
	RESET_FAKE(fan_set_count);
}

ZTEST_USER(fan, test_fan_init)
{
	cros_cbi_get_fw_config_fake.custom_fake = cros_cbi_get_fw_config_mock;
	fan_set_count_fake.custom_fake = fan_set_count_mock;

	hook_notify(HOOK_INIT);

	zassert_equal(1, cros_cbi_get_fw_config_fake.call_count);
	zassert_equal(0, fan_set_count_fake.call_count);
}

ZTEST_USER(fan, test_fan_init_no_fan)
{
	cros_cbi_get_fw_config_fake.custom_fake =
		cros_cbi_get_fw_config_mock_no_fan;
	fan_set_count_fake.custom_fake = fan_set_count_mock;

	hook_notify(HOOK_INIT);

	zassert_equal(1, cros_cbi_get_fw_config_fake.call_count);
	zassert_equal(1, fan_set_count_fake.call_count);
}

ZTEST_USER(fan, test_fan_init_error_reading_cbi)
{
	cros_cbi_get_fw_config_fake.custom_fake =
		cros_cbi_get_fw_config_mock_error;
	fan_set_count_fake.custom_fake = fan_set_count_mock;

	hook_notify(HOOK_INIT);

	zassert_equal(1, cros_cbi_get_fw_config_fake.call_count);
	zassert_equal(0, fan_set_count_fake.call_count);
}

ZTEST_SUITE(fan, NULL, NULL, fan_before, NULL, NULL);
