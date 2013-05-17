/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Test thermal engine.
 */

#include "board.h"
#include "common.h"
#include "console.h"
#include "hooks.h"
#include "host_command.h"
#include "temp_sensor.h"
#include "test_util.h"
#include "thermal.h"
#include "timer.h"
#include "util.h"

static int mock_temp[TEMP_SENSOR_COUNT];
static int fan_rpm;
static int cpu_throttled;
static int cpu_down;

extern struct thermal_config_t thermal_config[TEMP_SENSOR_TYPE_COUNT];
extern const int fan_speed[THERMAL_FAN_STEPS + 1];

/*****************************************************************************/
/* Mock functions */

int temp_sensor_read(enum temp_sensor_id id, int *temp_ptr)
{
	if (mock_temp[id] >= 0) {
		*temp_ptr = mock_temp[id];
		return EC_SUCCESS;
	} else {
		return -mock_temp[id];
	}
}

void pwm_set_fan_rpm_mode(int rpm_mode)
{
	/* Do nothing */
}

void pwm_set_fan_target_rpm(int rpm)
{
	fan_rpm = rpm;
}

void chipset_force_shutdown(void)
{
	cpu_down = 1;
}

void chipset_throttle_cpu(int throttled)
{
	cpu_throttled = throttled;
}

/*****************************************************************************/
/* Test utilities */

/* Test shorthands */
#define T_CPU TEMP_SENSOR_CPU
#define T_BOARD TEMP_SENSOR_BOARD
#define T_CASE TEMP_SENSOR_CASE
#define THRESHOLD(x, y) (thermal_config[x].thresholds[y])
#define FAN_THRESHOLD(x, y) THRESHOLD(x, THRESHOLD_COUNT + (y))

static void reset_mock_temp(void)
{
	int i;
	enum temp_sensor_type type;
	for (i = 0; i < TEMP_SENSOR_COUNT; ++i) {
		type = temp_sensors[i].type;
		mock_temp[i] = FAN_THRESHOLD(type, 0) - 1;
	}
}

static int wait_fan_rpm(int rpm, int timeout_secs)
{
	do {
		if (fan_rpm == rpm)
			return 1;
		usleep(SECOND);
	} while (timeout_secs--);

	return 0;
}

static int wait_value(int *v, int target, int timeout_secs)
{
	do {
		if (*v == target)
			return 1;
		usleep(SECOND);
	} while (timeout_secs--);

	return 0;
}

static int wait_set(int *v, int timeout_secs)
{
	return wait_value(v, 1, timeout_secs);
}

static int wait_clear(int *v, int timeout_secs)
{
	return wait_value(v, 0, timeout_secs);
}

/*****************************************************************************/
/* Tests */

static int test_init_val(void)
{
	/* Initial mock temperature values are all zero. */
	TEST_ASSERT(cpu_throttled == 0);
	TEST_ASSERT(cpu_down == 0);
	TEST_ASSERT(!(host_get_events() &
		      EC_HOST_EVENT_MASK(EC_HOST_EVENT_THERMAL_OVERLOAD)));
	TEST_ASSERT(!(host_get_events() &
		      EC_HOST_EVENT_MASK(EC_HOST_EVENT_THERMAL_SHUTDOWN)));

	return EC_SUCCESS;
}

static int test_cpu_fan(void)
{
	reset_mock_temp();

	/*
	 * Increase CPU temperature to first fan step and check if
	 * the fan comes up.
	 */
	mock_temp[T_CPU] = FAN_THRESHOLD(T_CPU, 0);
	TEST_ASSERT(wait_fan_rpm(fan_speed[1], 11));

	/* Increase CPU temperature to second fan step */
	mock_temp[T_CPU] = FAN_THRESHOLD(T_CPU, 1);
	TEST_ASSERT(wait_fan_rpm(fan_speed[2], 11));

	/* Test threshold hysteresis */
	mock_temp[T_CPU]--;
	usleep(15 * SECOND);
	TEST_ASSERT(fan_rpm == fan_speed[2]);

	/* Test action delay */
	mock_temp[T_CPU] = FAN_THRESHOLD(T_CPU, 4);
	usleep((temp_sensors[T_CPU].action_delay_sec - 1) * SECOND);
	TEST_ASSERT(fan_rpm == fan_speed[2]);
	mock_temp[T_CPU] = FAN_THRESHOLD(T_CPU, 0);

	return EC_SUCCESS;
}

static int test_safety(void)
{
	reset_mock_temp();

	/* Trigger CPU throttling */
	mock_temp[T_CPU] = THRESHOLD(T_CPU, THRESHOLD_WARNING);
	TEST_ASSERT(wait_set(&cpu_throttled, 11));
	TEST_ASSERT(host_get_events() &
		    EC_HOST_EVENT_MASK(EC_HOST_EVENT_THERMAL_OVERLOAD));

	/* Lower temperature. CPU not throttled anymore. */
	mock_temp[T_CPU] = THRESHOLD(T_CPU, THRESHOLD_WARNING) - 5;
	TEST_ASSERT(wait_clear(&cpu_throttled, 2));

	/* Thermal shutdown */
	mock_temp[T_CPU] = THRESHOLD(T_CPU, THRESHOLD_CPU_DOWN);
	TEST_ASSERT(wait_set(&cpu_down, 11));
	TEST_ASSERT(host_get_events() &
		    EC_HOST_EVENT_MASK(EC_HOST_EVENT_THERMAL_SHUTDOWN));

	mock_temp[T_CPU] = 0;
	usleep(SECOND);
	cpu_down = 0;

	mock_temp[T_CPU] = THRESHOLD(T_CPU, THRESHOLD_POWER_DOWN);
	TEST_ASSERT(wait_set(&cpu_down, 11));
	TEST_ASSERT(host_get_events() &
		    EC_HOST_EVENT_MASK(EC_HOST_EVENT_THERMAL_SHUTDOWN));

	return EC_SUCCESS;
}

static int test_sensor_failure(void)
{
	reset_mock_temp();

	/* Failure due to sensor not powered should be ignored */
	mock_temp[T_CPU] = -EC_ERROR_NOT_POWERED;
	usleep(5 * SECOND);
	TEST_ASSERT(!(host_get_events() &
		      EC_HOST_EVENT_MASK(EC_HOST_EVENT_THERMAL)));

	/* Other failure should be pumped up to host */
	mock_temp[T_CPU] = -EC_ERROR_UNKNOWN;
	usleep(5 * SECOND);
	TEST_ASSERT(host_get_events() &
		    EC_HOST_EVENT_MASK(EC_HOST_EVENT_THERMAL));

	return EC_SUCCESS;
}

static int check_assumption(void)
{
	TEST_ASSERT((int)TEMP_SENSOR_CPU == (int)TEMP_SENSOR_TYPE_CPU);
	TEST_ASSERT((int)TEMP_SENSOR_BOARD == (int)TEMP_SENSOR_TYPE_BOARD);
	TEST_ASSERT((int)TEMP_SENSOR_CASE == (int)TEMP_SENSOR_TYPE_CASE);

	TEST_ASSERT(temp_sensors[T_CPU].action_delay_sec != 0);

	TEST_ASSERT(thermal_config[T_CPU].config_flags &
		    THERMAL_CONFIG_WARNING_ON_FAIL);

	return EC_SUCCESS;
}

void run_test(void)
{
	test_reset();

	/* Test assumptions */
	RUN_TEST(check_assumption);

	RUN_TEST(test_init_val);
	RUN_TEST(test_cpu_fan);
	/* No tests for board and case temp sensors as they are ignored. */
	RUN_TEST(test_safety);
	RUN_TEST(test_sensor_failure);

	test_print_result();
}
