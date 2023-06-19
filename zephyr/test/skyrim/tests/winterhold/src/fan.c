/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include <zephyr/fff.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/ztest.h>

#include <cros_board_info.h>
#include <cros_cbi.h>
#include <fan.h>
#include <hooks.h>

FAKE_VOID_FUNC(fan_set_duty, int, int);
FAKE_VALUE_FUNC(int, fan_get_duty, int);

struct fan_conf conf;
struct fan_rpm rpm;

const struct fan_t fans[1] = { { .conf = &conf, .rpm = &rpm } };
struct fan_data fan_data[1];

K_TIMER_DEFINE(ktimer, NULL, NULL);

static void fan_set_duty_mock(int ch, int duty)
{
	zassert_equal(ch, 0);
	zassert_between_inclusive(duty, 0, 100);

	fan_data[ch].pwm_percent = duty;
}

static int fan_get_duty_mock(int ch)
{
	zassert_equal(ch, 0);
	zassert_between_inclusive(fan_data[ch].pwm_percent, 0, 100);

	return fan_data[ch].pwm_percent;
}

static int duty_to_rpm(int duty)
{
	const float ratio = 0.015;

	zassert_between_inclusive(duty, 0, 100);

	return (int)((float)duty / ratio);
}

static void fan_tick(void)
{
	int rpm_diff;
	int duty;

	duty = fan_data[0].pwm_percent;

	rpm_diff = duty_to_rpm(duty) - fan_data[0].rpm_actual;

	/* Clamp rpm_diff. This essentially emulates fan inertia. */
	if (rpm_diff > 0)
		rpm_diff = MIN(rpm_diff, 500);
	if (rpm_diff < 0)
		rpm_diff = MAX(rpm_diff, -500);

	fan_data[0].rpm_actual += rpm_diff;
}

static void fan_test_begin(void *fixture)
{
	ARG_UNUSED(fixture);
	RESET_FAKE(fan_set_duty);
	RESET_FAKE(fan_get_duty);

	fan_set_duty_fake.custom_fake = fan_set_duty_mock;
	fan_get_duty_fake.custom_fake = fan_get_duty_mock;

	/*
	 * This is normally read from DT.
	 * The problem is that we don't want to pull the entire fan framework
	 * for this test. Instead initialize it here.
	 * All the values come from
	 * zephyr/program/skyrim/winterhold/project.overlay.
	 */
	memset(&conf, 0, sizeof(conf));
	memset(&rpm, 0, sizeof(rpm));

	rpm.rpm_max = 4800;
	rpm.rpm_deviation = 3;

	memset(&fan_data[0], 0, sizeof(fan_data[0]));
}

/* Only FAN 0 should be supported. */
ZTEST(fan, test_fan_invalid_arg)
{
	enum fan_status status;

	status = board_override_fan_control_duty(1);
	zassert_equal(status, FAN_STATUS_FRUSTRATED);
}

/*
 * Check whether we can ramp up into rpm_max in 5s.
 * The time limit is selected on a per board basis.
 * It depends on the thermal capacity of the radiator and CPU TDP.
 * Basically we want to ensure that fan ramps up fast enough
 * to prevent the CPU from thermal throttling.
 */
ZTEST(fan, test_fan_max_rpm)
{
	enum fan_status status;
	int deviation;

	fan_data[0].rpm_target = rpm.rpm_max;
	k_timer_start(&ktimer, K_SECONDS(5), K_NO_WAIT);

	while (k_timer_remaining_ticks(&ktimer) != 0) {
		status = board_override_fan_control_duty(0);
		zassert_not_equal(status, FAN_STATUS_FRUSTRATED);
		fan_tick();
		k_sleep(K_TICKS(1));
	}

	deviation = rpm.rpm_deviation * rpm.rpm_max / 100;
	zassert_true(fan_set_duty_fake.call_count > 1);
	zassert_within(fan_data[0].rpm_actual, rpm.rpm_max, deviation);
	zassert_equal(status, FAN_STATUS_LOCKED);
}

/* Check for FAN_STATUS_STOPPED when the fan is in fact stopped. */
ZTEST(fan, test_fan_off)
{
	enum fan_status status;

	status = board_override_fan_control_duty(0);
	zassert_equal(status, FAN_STATUS_STOPPED);
	zassert_equal(fan_set_duty_fake.call_count, 0);
}

/* If we can't achieve selected RPM, FAN_STATUS_FRUSTRATED is expected. */
ZTEST(fan, test_fan_frustrated_max)
{
	enum fan_status status;

	/*
	 * 10 seconds should be more than enough for implementation to realize
	 * that it can't get up to 10k RPM
	 */
	fan_data[0].rpm_target = 10000;
	k_timer_start(&ktimer, K_SECONDS(10), K_NO_WAIT);

	while (k_timer_remaining_ticks(&ktimer) != 0) {
		status = board_override_fan_control_duty(0);
		if (status == FAN_STATUS_FRUSTRATED)
			break;

		fan_tick();
		k_sleep(K_TICKS(1));
	}
	zassert_true(fan_set_duty_fake.call_count > 1);
	zassert_equal(status, FAN_STATUS_FRUSTRATED);
}

ZTEST_SUITE(fan, NULL, NULL, fan_test_begin, NULL, NULL);
