/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "chipset.h"
#include "thermal.h"

#include <zephyr/devicetree.h>
#include <zephyr/fff.h>
#include <zephyr/ztest.h>

#include <cros_board_info.h>
#include <cros_cbi.h>
#include <fan.h>
#include <hooks.h>

FAKE_VOID_FUNC(fan_set_count, int);
FAKE_VALUE_FUNC(int, cros_cbi_get_fw_config, enum cbi_fw_config_field_id,
		uint32_t *);
FAKE_VALUE_FUNC(int, cbi_get_board_version, uint32_t *);
FAKE_VALUE_FUNC(int, chipset_in_state, int);
FAKE_VOID_FUNC(fan_set_rpm_mode, int, int);
FAKE_VOID_FUNC(fan_set_rpm_target, int, int);
FAKE_VALUE_FUNC(int, fan_get_rpm_target, int);

static bool fan_present;
static int board_version;

static int cros_cbi_get_fw_config_mock(enum cbi_fw_config_field_id field_id,
				       uint32_t *value)
{
	if (field_id != FW_FAN)
		return -EINVAL;

	*value = fan_present ? FW_FAN_PRESENT : FW_FAN_NOT_PRESENT;
	return 0;
}

static int cbi_get_board_version_mock(uint32_t *value)
{
	*value = board_version;
	return EC_SUCCESS;
}

enum chipset_state_mask fake_chipset_state;
int chipset_in_state_mock(int state_mask)
{
	return state_mask & fake_chipset_state;
}

int mock_rpm;
void fan_set_rpm_target_mock(int ch, int rpm)
{
	mock_rpm = rpm;
}
int fan_get_rpm_target_mock(int ch)
{
	return mock_rpm;
}

static void fan_before(void *fixture)
{
	ARG_UNUSED(fixture);
	RESET_FAKE(fan_set_count);
	RESET_FAKE(cros_cbi_get_fw_config);
	RESET_FAKE(cbi_get_board_version);

	cros_cbi_get_fw_config_fake.custom_fake = cros_cbi_get_fw_config_mock;
	cbi_get_board_version_fake.custom_fake = cbi_get_board_version_mock;

	chipset_in_state_fake.custom_fake = chipset_in_state_mock;
	fake_chipset_state = CHIPSET_STATE_ON;
	RESET_FAKE(fan_set_rpm_target);
	fan_set_rpm_target_fake.custom_fake = fan_set_rpm_target_mock;
	RESET_FAKE(fan_get_rpm_target);
	fan_get_rpm_target_fake.custom_fake = fan_get_rpm_target_mock;
}

ZTEST_SUITE(fan, NULL, NULL, fan_before, NULL, NULL);

ZTEST(fan, test_board_supports_pcore_ocp)
{
	/* Only supported for board version > 3. */
	board_version = 2;
	zassert_false(board_supports_pcore_ocp());
	board_version = 3;
	zassert_false(board_supports_pcore_ocp());
	board_version = 4;
	zassert_true(board_supports_pcore_ocp());
}

ZTEST(fan, test_fan_init)
{
	/* Only disable fans on board version >= 3. */
	fan_present = false;
	board_version = 2;
	fan_init();
	zassert_equal(fan_set_count_fake.call_count, 0);

	fan_present = true;
	board_version = 3;
	fan_init();
	zassert_equal(fan_set_count_fake.call_count, 0);

	fan_present = true;
	board_version = 4;
	fan_init();
	zassert_equal(fan_set_count_fake.call_count, 0);

	fan_present = false;
	board_version = 3;
	fan_init();
	zassert_equal(fan_set_count_fake.call_count, 1);

	fan_present = false;
	board_version = 4;
	fan_init();
	zassert_equal(fan_set_count_fake.call_count, 2);
}

ZTEST(fan, test_board_override_fan_control)
{
	int temp = 35;

	board_override_fan_control(0, &temp);
	zassert_equal(fan_set_rpm_target_fake.arg1_val, 0);

	temp = 45;
	board_override_fan_control(0, &temp);
	zassert_equal(fan_set_rpm_target_fake.arg1_val, 3000);

	temp = 55;
	board_override_fan_control(0, &temp);
	zassert_equal(fan_set_rpm_target_fake.arg1_val, 3500);

	temp = 65;
	board_override_fan_control(0, &temp);
	zassert_equal(fan_set_rpm_target_fake.arg1_val, 4000);

	temp = 75;
	board_override_fan_control(0, &temp);
	zassert_equal(fan_set_rpm_target_fake.arg1_val, 4500);

	temp = 85;
	board_override_fan_control(0, &temp);
	zassert_equal(fan_set_rpm_target_fake.arg1_val, 4800);

	temp = 75;
	board_override_fan_control(0, &temp);
	zassert_equal(fan_set_rpm_target_fake.arg1_val, 4800);

	temp = 65;
	board_override_fan_control(0, &temp);
	zassert_equal(fan_set_rpm_target_fake.arg1_val, 4000);

	temp = 55;
	board_override_fan_control(0, &temp);
	zassert_equal(fan_set_rpm_target_fake.arg1_val, 3500);

	temp = 45;
	board_override_fan_control(0, &temp);
	zassert_equal(fan_set_rpm_target_fake.arg1_val, 3000);

	temp = 38;
	board_override_fan_control(0, &temp);
	zassert_equal(fan_set_rpm_target_fake.arg1_val, 0);
}
