/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "chipset.h"
#include "thermal.h"

#include <zephyr/devicetree.h>
#include <zephyr/fff.h>
#include <zephyr/ztest.h>

#include <fan.h>
#include <hooks.h>

bool board_supports_pcore_ocp(void);
void fan_init(void);

FAKE_VOID_FUNC(fan_set_count, int);

FAKE_VALUE_FUNC(int, chipset_in_state, int);
FAKE_VOID_FUNC(fan_set_rpm_mode, int, int);
FAKE_VOID_FUNC(fan_set_rpm_target, int, int);
FAKE_VALUE_FUNC(int, fan_get_rpm_target, int);

/* Normally implemented in zephyr/shim/src/espi.c */
void lpc_keyboard_resume_irq(void)
{
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

	chipset_in_state_fake.custom_fake = chipset_in_state_mock;
	fake_chipset_state = CHIPSET_STATE_ON;
	RESET_FAKE(fan_set_rpm_target);
	fan_set_rpm_target_fake.custom_fake = fan_set_rpm_target_mock;
	RESET_FAKE(fan_get_rpm_target);
	fan_get_rpm_target_fake.custom_fake = fan_get_rpm_target_mock;
}

ZTEST_SUITE(fan, NULL, NULL, fan_before, NULL, NULL);

ZTEST(fan, test_board_override_fan_control)
{
	int temp = 35;

	board_override_fan_control(0, &temp);
	zassert_equal(fan_set_rpm_target_fake.arg1_val, 0);

	temp = 40;
	board_override_fan_control(0, &temp);
	zassert_equal(fan_set_rpm_target_fake.arg1_val, 2700);

	temp = 43;
	board_override_fan_control(0, &temp);
	zassert_equal(fan_set_rpm_target_fake.arg1_val, 2925);

	temp = 46;
	board_override_fan_control(0, &temp);
	zassert_equal(fan_set_rpm_target_fake.arg1_val, 3300);

	temp = 49;
	board_override_fan_control(0, &temp);
	zassert_equal(fan_set_rpm_target_fake.arg1_val, 3585);

	temp = 52;
	board_override_fan_control(0, &temp);
	zassert_equal(fan_set_rpm_target_fake.arg1_val, 3900);

	temp = 55;
	board_override_fan_control(0, &temp);
	zassert_equal(fan_set_rpm_target_fake.arg1_val, 4255);

	temp = 65;
	board_override_fan_control(0, &temp);
	zassert_equal(fan_set_rpm_target_fake.arg1_val, 4775);

	temp = 60;
	board_override_fan_control(0, &temp);
	zassert_equal(fan_set_rpm_target_fake.arg1_val, 4255);

	temp = 51;
	board_override_fan_control(0, &temp);
	zassert_equal(fan_set_rpm_target_fake.arg1_val, 3900);

	temp = 48;
	board_override_fan_control(0, &temp);
	zassert_equal(fan_set_rpm_target_fake.arg1_val, 3585);

	temp = 45;
	board_override_fan_control(0, &temp);
	zassert_equal(fan_set_rpm_target_fake.arg1_val, 3300);

	temp = 42;
	board_override_fan_control(0, &temp);
	zassert_equal(fan_set_rpm_target_fake.arg1_val, 2925);

	temp = 39;
	board_override_fan_control(0, &temp);
	zassert_equal(fan_set_rpm_target_fake.arg1_val, 2700);

	temp = 35;
	board_override_fan_control(0, &temp);
	zassert_equal(fan_set_rpm_target_fake.arg1_val, 0);
}

ZTEST(fan, test_board_override_fan_control_s0ix)
{
	int temp = 65;

	fake_chipset_state = CHIPSET_STATE_SUSPEND;

	board_override_fan_control(0, &temp);
	zassert_equal(fan_set_rpm_target_fake.arg1_val, 0);
}
