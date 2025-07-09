/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "chipset.h"
#include "common.h"
#include "cros_board_info.h"
#include "cros_cbi.h"
#include "ec_commands.h"
#include "fan.h"
#include "host_command.h"
#include "thermal.h"

#include <zephyr/fff.h>
#include <zephyr/ztest.h>

FAKE_VOID_FUNC(host_set_single_event, enum host_event_code);
FAKE_VALUE_FUNC(int, cros_cbi_get_fw_config, enum cbi_fw_config_field_id,
		uint32_t *);
FAKE_VALUE_FUNC(int, chipset_in_state, int);

FAKE_VALUE_FUNC(uint8_t *, host_get_memmap, int);

void fan_init(void);

static void test_before(void *fixture)
{
	RESET_FAKE(cros_cbi_get_fw_config);
	RESET_FAKE(chipset_in_state);
}
ZTEST_SUITE(jubilant_fan, NULL, NULL, test_before, NULL, NULL);

static int thermal_solution;

static int cbi_get_thermal_fw_config(enum cbi_fw_config_field_id field,
				     uint32_t *value)
{
	zassert_equal(field, FW_THERMAL);
	*value = thermal_solution;
	return 0;
}

static int chipset_state;

static int chipset_in_state_mock(int state_mask)
{
	if (state_mask & chipset_state)
		return 1;

	return 0;
}

ZTEST(jubilant_fan, test_fan_table)
{
	int temp[3];

	/* Initialize pwm fam (pwm_fan_init) */
	fan_channel_setup(0, FAN_USE_RPM_MODE);
	fan_set_enabled(0, 1);

	/* Test fan table for default table */
	cros_cbi_get_fw_config_fake.custom_fake = cbi_get_thermal_fw_config;
	thermal_solution = FW_THERMAL_passive;
	fan_init();

	/* Turn on fan when chipset state on. */
	chipset_in_state_fake.custom_fake = chipset_in_state_mock;
	chipset_state = CHIPSET_STATE_ON;

	/* level_0 */
	temp[0] = 0;
	temp[1] = 25;
	temp[2] = 0;
	board_override_fan_control(0, temp);
	zassert_equal(fan_get_rpm_mode(0), 1);
	zassert_equal(fan_get_rpm_target(0), 0);

	/* level_1 */
	temp[0] = 47;
	temp[1] = 37;
	temp[2] = 50;
	board_override_fan_control(0, temp);
	zassert_equal(fan_get_rpm_mode(0), 1);
	zassert_equal(fan_get_rpm_target(0), 2500);

	/* level_2 */
	temp[0] = 50;
	temp[1] = 40;
	temp[2] = 50;
	board_override_fan_control(0, temp);
	zassert_equal(fan_get_rpm_mode(0), 1);
	zassert_equal(fan_get_rpm_target(0), 2900);

	/* level_3 */
	temp[0] = 54;
	temp[1] = 43;
	temp[2] = 55;
	board_override_fan_control(0, temp);
	zassert_equal(fan_get_rpm_mode(0), 1);
	zassert_equal(fan_get_rpm_target(0), 3300);

	/* level_4 */
	temp[0] = 57;
	temp[1] = 46;
	temp[2] = 61;
	board_override_fan_control(0, temp);
	zassert_equal(fan_get_rpm_mode(0), 1);
	zassert_equal(fan_get_rpm_target(0), 3650);

	/* level_5 */
	temp[0] = 61;
	temp[1] = 49;
	temp[2] = 65;
	board_override_fan_control(0, temp);
	zassert_equal(fan_get_rpm_mode(0), 1);
	zassert_equal(fan_get_rpm_target(0), 4100);

	/* level_6 */
	temp[0] = 61;
	temp[1] = 52;
	temp[2] = 65;
	board_override_fan_control(0, temp);
	zassert_equal(fan_get_rpm_mode(0), 1);
	zassert_equal(fan_get_rpm_target(0), 4500);

	/* level_7 */
	temp[0] = 61;
	temp[1] = 60;
	temp[2] = 65;
	board_override_fan_control(0, temp);
	zassert_equal(fan_get_rpm_mode(0), 1);
	zassert_equal(fan_get_rpm_target(0), 5300);

	/* level_8 */
	temp[0] = 61;
	temp[1] = 66;
	temp[2] = 65;
	board_override_fan_control(0, temp);
	zassert_equal(fan_get_rpm_mode(0), 1);
	zassert_equal(fan_get_rpm_target(0), 5800);

	/* decrease temp to level_7 */
	temp[0] = 57;
	temp[1] = 59;
	temp[2] = 57;
	board_override_fan_control(0, temp);
	zassert_equal(fan_get_rpm_mode(0), 1);
	zassert_equal(fan_get_rpm_target(0), 5300);

	/* decrease temp to level_6 */
	temp[0] = 57;
	temp[1] = 51;
	temp[2] = 57;
	board_override_fan_control(0, temp);
	zassert_equal(fan_get_rpm_mode(0), 1);
	zassert_equal(fan_get_rpm_target(0), 4500);

	/* decrease temp to level_5 */
	temp[0] = 57;
	temp[1] = 48;
	temp[2] = 57;
	board_override_fan_control(0, temp);
	zassert_equal(fan_get_rpm_mode(0), 1);
	zassert_equal(fan_get_rpm_target(0), 4100);

	/* decrease temp to level_4 */
	temp[0] = 53;
	temp[1] = 45;
	temp[2] = 55;
	board_override_fan_control(0, temp);
	zassert_equal(fan_get_rpm_mode(0), 1);
	zassert_equal(fan_get_rpm_target(0), 3650);

	/* decrease temp to level_3 */
	temp[0] = 50;
	temp[1] = 42;
	temp[2] = 51;
	board_override_fan_control(0, temp);
	zassert_equal(fan_get_rpm_mode(0), 1);
	zassert_equal(fan_get_rpm_target(0), 3300);

	/* decrease temp to level_2 */
	temp[0] = 46;
	temp[1] = 39;
	temp[2] = 50;
	board_override_fan_control(0, temp);
	zassert_equal(fan_get_rpm_mode(0), 1);
	zassert_equal(fan_get_rpm_target(0), 2900);

	/* decrease temp to level_1 */
	temp[0] = 43;
	temp[1] = 36;
	temp[2] = 50;
	board_override_fan_control(0, temp);
	zassert_equal(fan_get_rpm_mode(0), 1);
	zassert_equal(fan_get_rpm_target(0), 2500);

	/* decrease temp to level_0 */
	temp[0] = 39;
	temp[1] = 34;
	temp[2] = 50;
	board_override_fan_control(0, temp);
	zassert_equal(fan_get_rpm_mode(0), 1);
	zassert_equal(fan_get_rpm_target(0), 0);

	/* Turn off fan when chipset suspend or shutdown */
	chipset_in_state_fake.custom_fake = chipset_in_state_mock;
	chipset_state = CHIPSET_STATE_STANDBY;
	board_override_fan_control(0, temp);
	zassert_equal(fan_get_rpm_mode(0), 1);
	zassert_equal(fan_get_rpm_target(0), 0);
}
