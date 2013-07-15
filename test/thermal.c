/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Test thermal engine.
 */

#include "common.h"
#include "console.h"
#include "hooks.h"
#include "host_command.h"
#include "printf.h"
#include "temp_sensor.h"
#include "test_util.h"
#include "thermal.h"
#include "timer.h"
#include "util.h"

static int mock_temp[TEMP_SENSOR_COUNT];
static int fan_rpm;
static int fan_rpm_mode = 1;
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
	fan_rpm_mode = rpm_mode;
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

	mock_temp[T_CPU] = 0;
	cpu_down = 0;

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

static int test_sensor_info(void)
{
	struct ec_params_temp_sensor_get_info params;
	struct ec_response_temp_sensor_get_info resp;
	int i;

	for (i = 0; i < TEMP_SENSOR_COUNT; ++i) {
		params.id = i;
		TEST_ASSERT(test_send_host_command(
				EC_CMD_TEMP_SENSOR_GET_INFO,
				0, &params, sizeof(params),
				&resp, sizeof(resp)) == EC_RES_SUCCESS);
		TEST_ASSERT_ARRAY_EQ(resp.sensor_name,
				     temp_sensors[i].name,
				     strlen(resp.sensor_name));
		TEST_ASSERT(resp.sensor_type == temp_sensors[i].type);
	}

	params.id = TEMP_SENSOR_COUNT;
	TEST_ASSERT(test_send_host_command(
			EC_CMD_TEMP_SENSOR_GET_INFO,
			0, &params, sizeof(params),
			&resp, sizeof(resp)) != EC_RES_SUCCESS);

	return EC_SUCCESS;
}

static int set_threshold(int type, int threshold_id, int val)
{
	struct ec_params_thermal_set_threshold params;

	params.sensor_type = type;
	params.threshold_id = threshold_id;
	params.value = val;

	return test_send_host_command(EC_CMD_THERMAL_SET_THRESHOLD, 0, &params,
				      sizeof(params), NULL, 0);
}

static int get_threshold(int type, int threshold_id, int *val)
{
	struct ec_params_thermal_get_threshold params;
	struct ec_response_thermal_get_threshold resp;
	int rv;

	params.sensor_type = type;
	params.threshold_id = threshold_id;

	rv = test_send_host_command(EC_CMD_THERMAL_GET_THRESHOLD, 0, &params,
				    sizeof(params), &resp, sizeof(resp));
	if (rv != EC_RES_SUCCESS)
		return rv;

	*val = resp.value;
	return EC_RES_SUCCESS;
}

static int verify_threshold(int type, int threshold_id, int val)
{
	int actual_val;

	if (get_threshold(type, threshold_id, &actual_val) != EC_RES_SUCCESS)
		return 0;
	return val == actual_val;
}

static int test_threshold_hostcmd(void)
{
	reset_mock_temp();

	/* Verify thresholds */
	TEST_ASSERT(verify_threshold(T_CPU, THRESHOLD_WARNING,
				     THRESHOLD(T_CPU, THRESHOLD_WARNING)));
	TEST_ASSERT(verify_threshold(T_BOARD, THRESHOLD_WARNING,
				     THRESHOLD(T_BOARD, THRESHOLD_WARNING)));
	TEST_ASSERT(verify_threshold(T_CPU, THRESHOLD_CPU_DOWN,
				     THRESHOLD(T_CPU, THRESHOLD_CPU_DOWN)));

	/* Lower CPU throttling threshold and trigger */
	TEST_ASSERT(set_threshold(T_CPU, THRESHOLD_WARNING, 350) ==
		    EC_RES_SUCCESS);
	mock_temp[T_CPU] = 355;
	TEST_ASSERT(wait_set(&cpu_throttled, 11));
	TEST_ASSERT(host_get_events() &
		    EC_HOST_EVENT_MASK(EC_HOST_EVENT_THERMAL_OVERLOAD));

	/* Lower thermal shutdown threshold */
	TEST_ASSERT(set_threshold(T_CPU, THRESHOLD_CPU_DOWN, 353) ==
		    EC_RES_SUCCESS);
	TEST_ASSERT(wait_set(&cpu_down, 11));
	TEST_ASSERT(host_get_events() &
		    EC_HOST_EVENT_MASK(EC_HOST_EVENT_THERMAL_SHUTDOWN));

	/* Clear */
	mock_temp[T_CPU] = 0;
	TEST_ASSERT(wait_clear(&cpu_throttled, 2));
	cpu_down = 0;

	return EC_SUCCESS;
}

static int test_threshold_console_cmd(void)
{
	char buf[100];

	reset_mock_temp();

	/* Lower CPU threshold and trigger */
	snprintf(buf, 100, "thermalconf %d %d 330\n", T_CPU, THRESHOLD_WARNING);
	UART_INJECT(buf);
	msleep(100);
	mock_temp[T_CPU] = 335;
	TEST_ASSERT(wait_set(&cpu_throttled, 11));
	TEST_ASSERT(host_get_events() &
		    EC_HOST_EVENT_MASK(EC_HOST_EVENT_THERMAL_OVERLOAD));

	/* Set first fan step to 280 K */
	snprintf(buf, 100, "thermalfan %d 0 280\n", T_CPU);
	UART_INJECT(buf);
	msleep(100);
	mock_temp[T_CPU] = 280;
	TEST_ASSERT(wait_fan_rpm(fan_speed[1], 11));

	return EC_SUCCESS;
}

static int test_invalid_hostcmd(void)
{
	int dummy;

	TEST_ASSERT(set_threshold(TEMP_SENSOR_TYPE_COUNT, THRESHOLD_WARNING,
				  100) != EC_RES_SUCCESS);
	TEST_ASSERT(set_threshold(T_CPU, THRESHOLD_COUNT + THERMAL_FAN_STEPS,
				  100) != EC_RES_SUCCESS);
	TEST_ASSERT(get_threshold(TEMP_SENSOR_TYPE_COUNT, THRESHOLD_WARNING,
				  &dummy) != EC_RES_SUCCESS);
	TEST_ASSERT(get_threshold(T_CPU, THRESHOLD_COUNT + THERMAL_FAN_STEPS,
				  &dummy) != EC_RES_SUCCESS);

	return EC_SUCCESS;
}

static int test_auto_fan_ctrl(void)
{
	reset_mock_temp();

	/* Disable fan control */
	pwm_set_fan_rpm_mode(0);
	thermal_control_fan(0);

	/*
	 * Increase CPU temperature to first fan step and check the fan
	 * doesn't come up.
	 */
	mock_temp[T_CPU] = FAN_THRESHOLD(T_CPU, 0);
	TEST_ASSERT(!wait_fan_rpm(fan_speed[1], 11));

	/* Enable fan control */
	TEST_ASSERT(test_send_host_command(EC_CMD_THERMAL_AUTO_FAN_CTRL, 0,
					   NULL, 0, NULL, 0) == EC_RES_SUCCESS);
	TEST_ASSERT(fan_rpm_mode == 1);
	TEST_ASSERT(wait_fan_rpm(fan_speed[1], 11));

	/* Disable fan control */
	pwm_set_fan_rpm_mode(0);
	thermal_control_fan(0);

	/* Increase CPU temperature to second fan step */
	mock_temp[T_CPU] = FAN_THRESHOLD(T_CPU, 1);
	TEST_ASSERT(!wait_fan_rpm(fan_speed[2], 11));

	/* Enable fan control by console command */
	UART_INJECT("autofan\n");
	msleep(100);
	TEST_ASSERT(fan_rpm_mode == 1);
	TEST_ASSERT(wait_fan_rpm(fan_speed[2], 11));


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
	RUN_TEST(test_auto_fan_ctrl);
	RUN_TEST(test_sensor_info);
	RUN_TEST(test_threshold_hostcmd);
	RUN_TEST(test_invalid_hostcmd);
	RUN_TEST(test_threshold_console_cmd);

	test_print_result();
}
