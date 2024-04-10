/* Copyright 2013 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Test thermal engine.
 */

#include "builtin/assert.h"
#include "common.h"
#include "console.h"
#include "driver/temp_sensor/thermistor.h"
#include "fan.h"
#include "hooks.h"
#include "host_command.h"
#include "printf.h"
#include "temp_sensor.h"
#include "test_util.h"
#include "thermal.h"
#include "timer.h"
#include "util.h"

/*****************************************************************************/
/* Exported data */

struct ec_thermal_config thermal_params[TEMP_SENSOR_COUNT];

/* The tests below make some assumptions. */
BUILD_ASSERT(TEMP_SENSOR_COUNT == 4);
BUILD_ASSERT(EC_TEMP_THRESH_COUNT == 3);

/*****************************************************************************/
/* Mock functions */

static int mock_temp[TEMP_SENSOR_COUNT];
static int host_throttled;
static int cpu_throttled;
static int cpu_shutdown;
static int fan_pct;
static int no_temps_read;

int mock_temp_get_val(int idx, int *temp_ptr)
{
	if (mock_temp[idx] >= 0) {
		*temp_ptr = mock_temp[idx];
		return EC_SUCCESS;
	}

	return EC_ERROR_NOT_POWERED;
}

void chipset_force_shutdown(void)
{
	cpu_shutdown = 1;
}

void chipset_throttle_cpu(int throttled)
{
	cpu_throttled = throttled;
}

void host_throttle_cpu(int throttled)
{
	host_throttled = throttled;
}

void fan_set_percent_needed(int fan, int pct)
{
	fan_pct = pct;
}

void smi_sensor_failure_warning(void)
{
	no_temps_read = 1;
}

/*****************************************************************************/
/* Test utilities */

static void set_temps(int t0, int t1, int t2, int t3)
{
	mock_temp[0] = t0;
	mock_temp[1] = t1;
	mock_temp[2] = t2;
	mock_temp[3] = t3;
}

static void all_temps(int t)
{
	set_temps(t, t, t, t);
}

static void reset_mocks(void)
{
	/* Ignore all sensors */
	memset(thermal_params, 0, sizeof(thermal_params));

	/* All sensors report error anyway */
	set_temps(-1, -1, -1, -1);

	/* Reset expectations */
	host_throttled = 0;
	cpu_throttled = 0;
	cpu_shutdown = 0;
	fan_pct = 0;
	no_temps_read = 0;
}

/*****************************************************************************/
/* Tests */

static int test_init_val(void)
{
	reset_mocks();
	crec_sleep(2);

	TEST_ASSERT(host_throttled == 0);
	TEST_ASSERT(cpu_throttled == 0);
	TEST_ASSERT(cpu_shutdown == 0);
	TEST_ASSERT(fan_pct == 0);
	TEST_ASSERT(no_temps_read);

	crec_sleep(2);

	TEST_ASSERT(host_throttled == 0);
	TEST_ASSERT(cpu_throttled == 0);
	TEST_ASSERT(cpu_shutdown == 0);
	TEST_ASSERT(fan_pct == 0);
	TEST_ASSERT(no_temps_read);

	return EC_SUCCESS;
}

static int test_sensors_can_be_read(void)
{
	reset_mocks();
	mock_temp[2] = 100;

	crec_sleep(2);

	TEST_ASSERT(host_throttled == 0);
	TEST_ASSERT(cpu_throttled == 0);
	TEST_ASSERT(cpu_shutdown == 0);
	TEST_ASSERT(fan_pct == 0);
	TEST_ASSERT(no_temps_read == 0);

	return EC_SUCCESS;
}

static int test_one_fan(void)
{
	reset_mocks();
	thermal_params[2].temp_fan_off = 100;
	thermal_params[2].temp_fan_max = 200;

	all_temps(50);
	crec_sleep(2);
	TEST_ASSERT(fan_pct == 0);

	all_temps(100);
	crec_sleep(2);
	TEST_ASSERT(fan_pct == 0);

	all_temps(101);
	crec_sleep(2);
	TEST_ASSERT(fan_pct == 1);

	all_temps(130);
	crec_sleep(2);
	TEST_ASSERT(fan_pct == 30);

	all_temps(150);
	crec_sleep(2);
	TEST_ASSERT(fan_pct == 50);

	all_temps(170);
	crec_sleep(2);
	TEST_ASSERT(fan_pct == 70);

	all_temps(200);
	crec_sleep(2);
	TEST_ASSERT(fan_pct == 100);

	all_temps(300);
	crec_sleep(2);
	TEST_ASSERT(fan_pct == 100);

	return EC_SUCCESS;
}

static int test_two_fans(void)
{
	reset_mocks();

	thermal_params[1].temp_fan_off = 120;
	thermal_params[1].temp_fan_max = 160;
	thermal_params[2].temp_fan_off = 100;
	thermal_params[2].temp_fan_max = 200;

	all_temps(50);
	crec_sleep(2);
	TEST_ASSERT(fan_pct == 0);

	all_temps(100);
	crec_sleep(2);
	TEST_ASSERT(fan_pct == 0);

	all_temps(101);
	crec_sleep(2);
	TEST_ASSERT(fan_pct == 1);

	all_temps(130);
	crec_sleep(2);
	/* fan 2 is still higher */
	TEST_ASSERT(fan_pct == 30);

	all_temps(150);
	crec_sleep(2);
	/* now fan 1 is higher: 150 = 75% of [120-160] */
	TEST_ASSERT(fan_pct == 75);

	all_temps(170);
	crec_sleep(2);
	/* fan 1 is maxed now */
	TEST_ASSERT(fan_pct == 100);

	all_temps(200);
	crec_sleep(2);
	TEST_ASSERT(fan_pct == 100);

	all_temps(300);
	crec_sleep(2);
	TEST_ASSERT(fan_pct == 100);

	return EC_SUCCESS;
}

static int test_all_fans(void)
{
	reset_mocks();

	thermal_params[0].temp_fan_off = 20;
	thermal_params[0].temp_fan_max = 60;
	thermal_params[1].temp_fan_off = 120;
	thermal_params[1].temp_fan_max = 160;
	thermal_params[2].temp_fan_off = 100;
	thermal_params[2].temp_fan_max = 200;
	thermal_params[3].temp_fan_off = 300;
	thermal_params[3].temp_fan_max = 500;

	set_temps(1, 1, 1, 1);
	crec_sleep(2);
	TEST_ASSERT(fan_pct == 0);

	/* Each sensor has its own range */
	set_temps(40, 0, 0, 0);
	crec_sleep(2);
	TEST_ASSERT(fan_pct == 50);

	set_temps(0, 140, 0, 0);
	crec_sleep(2);
	TEST_ASSERT(fan_pct == 50);

	set_temps(0, 0, 150, 0);
	crec_sleep(2);
	TEST_ASSERT(fan_pct == 50);

	set_temps(0, 0, 0, 400);
	crec_sleep(2);
	TEST_ASSERT(fan_pct == 50);

	set_temps(60, 0, 0, 0);
	crec_sleep(2);
	TEST_ASSERT(fan_pct == 100);

	set_temps(0, 160, 0, 0);
	crec_sleep(2);
	TEST_ASSERT(fan_pct == 100);

	set_temps(0, 0, 200, 0);
	crec_sleep(2);
	TEST_ASSERT(fan_pct == 100);

	set_temps(0, 0, 0, 500);
	crec_sleep(2);
	TEST_ASSERT(fan_pct == 100);

	/* But sensor 0 needs the most cooling */
	all_temps(20);
	crec_sleep(2);
	TEST_ASSERT(fan_pct == 0);

	all_temps(21);
	crec_sleep(2);
	TEST_ASSERT(fan_pct == 2);

	all_temps(30);
	crec_sleep(2);
	TEST_ASSERT(fan_pct == 25);

	all_temps(40);
	crec_sleep(2);
	TEST_ASSERT(fan_pct == 50);

	all_temps(50);
	crec_sleep(2);
	TEST_ASSERT(fan_pct == 75);

	all_temps(60);
	crec_sleep(2);
	TEST_ASSERT(fan_pct == 100);

	all_temps(65);
	crec_sleep(2);
	TEST_ASSERT(fan_pct == 100);

	return EC_SUCCESS;
}

static int test_one_limit(void)
{
	reset_mocks();
	thermal_params[2].temp_host[EC_TEMP_THRESH_WARN] = 100;
	thermal_params[2].temp_host[EC_TEMP_THRESH_HIGH] = 200;
	thermal_params[2].temp_host[EC_TEMP_THRESH_HALT] = 300;

	all_temps(50);
	crec_sleep(2);
	TEST_ASSERT(host_throttled == 0);
	TEST_ASSERT(cpu_throttled == 0);
	TEST_ASSERT(cpu_shutdown == 0);

	all_temps(100);
	crec_sleep(2);
	TEST_ASSERT(host_throttled == 0);
	TEST_ASSERT(cpu_throttled == 0);
	TEST_ASSERT(cpu_shutdown == 0);

	all_temps(101);
	crec_sleep(2);
	TEST_ASSERT(host_throttled == 1);
	TEST_ASSERT(cpu_throttled == 0);
	TEST_ASSERT(cpu_shutdown == 0);

	all_temps(100);
	crec_sleep(2);
	TEST_ASSERT(host_throttled == 1);
	TEST_ASSERT(cpu_throttled == 0);
	TEST_ASSERT(cpu_shutdown == 0);

	all_temps(99);
	crec_sleep(2);
	TEST_ASSERT(host_throttled == 0);
	TEST_ASSERT(cpu_throttled == 0);
	TEST_ASSERT(cpu_shutdown == 0);

	all_temps(199);
	crec_sleep(2);
	TEST_ASSERT(host_throttled == 1);
	TEST_ASSERT(cpu_throttled == 0);
	TEST_ASSERT(cpu_shutdown == 0);

	all_temps(200);
	crec_sleep(2);
	TEST_ASSERT(host_throttled == 1);
	TEST_ASSERT(cpu_throttled == 0);
	TEST_ASSERT(cpu_shutdown == 0);

	all_temps(201);
	crec_sleep(2);
	TEST_ASSERT(host_throttled == 1);
	TEST_ASSERT(cpu_throttled == 1);
	TEST_ASSERT(cpu_shutdown == 0);

	all_temps(200);
	crec_sleep(2);
	TEST_ASSERT(host_throttled == 1);
	TEST_ASSERT(cpu_throttled == 1);
	TEST_ASSERT(cpu_shutdown == 0);

	all_temps(199);
	crec_sleep(2);
	TEST_ASSERT(host_throttled == 1);
	TEST_ASSERT(cpu_throttled == 0);
	TEST_ASSERT(cpu_shutdown == 0);

	all_temps(99);
	crec_sleep(2);
	TEST_ASSERT(host_throttled == 0);
	TEST_ASSERT(cpu_throttled == 0);
	TEST_ASSERT(cpu_shutdown == 0);

	all_temps(201);
	crec_sleep(2);
	TEST_ASSERT(host_throttled == 1);
	TEST_ASSERT(cpu_throttled == 1);
	TEST_ASSERT(cpu_shutdown == 0);

	all_temps(99);
	crec_sleep(2);
	TEST_ASSERT(host_throttled == 0);
	TEST_ASSERT(cpu_throttled == 0);
	TEST_ASSERT(cpu_shutdown == 0);

	all_temps(301);
	crec_sleep(2);
	TEST_ASSERT(host_throttled == 1);
	TEST_ASSERT(cpu_throttled == 1);
	TEST_ASSERT(cpu_shutdown == 1);

	/* We probably won't be able to read the CPU temp while shutdown,
	 * so nothing will change. */
	all_temps(-1);
	crec_sleep(2);
	TEST_ASSERT(host_throttled == 1);
	TEST_ASSERT(cpu_throttled == 1);
	/* cpu_shutdown is only set for testing purposes. The thermal task
	 * doesn't do anything that could clear it. */

	all_temps(50);
	crec_sleep(2);
	TEST_ASSERT(host_throttled == 0);
	TEST_ASSERT(cpu_throttled == 0);

	return EC_SUCCESS;
}

static int test_several_limits(void)
{
	reset_mocks();

	thermal_params[1].temp_host[EC_TEMP_THRESH_WARN] = 150;
	thermal_params[1].temp_host[EC_TEMP_THRESH_HIGH] = 200;
	thermal_params[1].temp_host[EC_TEMP_THRESH_HALT] = 250;

	thermal_params[2].temp_host[EC_TEMP_THRESH_WARN] = 100;
	thermal_params[2].temp_host[EC_TEMP_THRESH_HIGH] = 200;
	thermal_params[2].temp_host[EC_TEMP_THRESH_HALT] = 300;

	thermal_params[3].temp_host[EC_TEMP_THRESH_WARN] = 20;
	thermal_params[3].temp_host[EC_TEMP_THRESH_HIGH] = 30;
	thermal_params[3].temp_host[EC_TEMP_THRESH_HALT] = 40;

	set_temps(500, 100, 150, 10);
	crec_sleep(2);
	TEST_ASSERT(host_throttled == 1); /* 1=low, 2=warn, 3=low */
	TEST_ASSERT(cpu_throttled == 0);
	TEST_ASSERT(cpu_shutdown == 0);

	set_temps(500, 50, -1, 10); /* 1=low, 2=X, 3=low */
	crec_sleep(2);
	TEST_ASSERT(host_throttled == 0);
	TEST_ASSERT(cpu_throttled == 0);
	TEST_ASSERT(cpu_shutdown == 0);

	set_temps(500, 170, 210, 10); /* 1=warn, 2=high, 3=low */
	crec_sleep(2);
	TEST_ASSERT(host_throttled == 1);
	TEST_ASSERT(cpu_throttled == 1);
	TEST_ASSERT(cpu_shutdown == 0);

	set_temps(500, 100, 50, 40); /* 1=low, 2=low, 3=high */
	crec_sleep(2);
	TEST_ASSERT(host_throttled == 1);
	TEST_ASSERT(cpu_throttled == 1);
	TEST_ASSERT(cpu_shutdown == 0);

	set_temps(500, 100, 50, 41); /* 1=low, 2=low, 3=shutdown */
	crec_sleep(2);
	TEST_ASSERT(host_throttled == 1);
	TEST_ASSERT(cpu_throttled == 1);
	TEST_ASSERT(cpu_shutdown == 1);

	all_temps(0); /* reset from shutdown */
	crec_sleep(2);
	TEST_ASSERT(host_throttled == 0);
	TEST_ASSERT(cpu_throttled == 0);

	return EC_SUCCESS;
}

/* Tests for ncp15wb thermistor ADC-to-temp calculation */
#define LOW_ADC_TEST_VALUE 887 /* 0 C */
#define HIGH_ADC_TEST_VALUE 100 /* > 100C */

static int test_ncp15wb_adc_to_temp(void)
{
	int i;
	uint8_t temp;
	uint8_t new_temp;

	/* ADC value to temperature table, data from datasheet */
	struct {
		int adc;
		int temp;
	} adc_temp_datapoints[] = {
		{ 615, 30 }, { 561, 35 }, { 508, 40 },
		{ 407, 50 }, { 315, 60 }, { 243, 70 },
		{ 186, 80 }, { 140, 90 }, { 107, 100 },
	};

	/*
	 * Verify that calculated temp is decreasing for entire ADC range,
	 * and that a tick down in ADC value results in no more than 1C
	 * decrease.
	 */
	i = LOW_ADC_TEST_VALUE;
	temp = ncp15wb_calculate_temp(i);

	while (--i > HIGH_ADC_TEST_VALUE) {
		new_temp = ncp15wb_calculate_temp(i);
		TEST_ASSERT(new_temp == temp || new_temp == temp + 1);
		temp = new_temp;
	}

	/* Verify several datapoints are within 1C accuracy */
	for (i = 0; i < ARRAY_SIZE(adc_temp_datapoints); ++i) {
		temp = ncp15wb_calculate_temp(adc_temp_datapoints[i].adc);
		ASSERT(temp >= adc_temp_datapoints[i].temp - 1 &&
		       temp <= adc_temp_datapoints[i].temp + 1);
	}

	return EC_SUCCESS;
}

#define THERMISTOR_SCALING_FACTOR 13
static int test_thermistor_linear_interpolate(void)
{
	int i, t, t0;
	uint16_t mv;
	/* Simple test case - a straight line. */
	struct thermistor_data_pair line_data[] = { { 100, 0 }, { 0, 100 } };
	struct thermistor_info line_info = {
		.scaling_factor = 1,
		.num_pairs = ARRAY_SIZE(line_data),
		.data = line_data,
	};
	/*
	 * Modelled test case - Data derived from Seinhart-Hart equation in a
	 * resistor divider circuit with Vdd=3300mV, R = 51.1Kohm, and Murata
	 * NCP15WB-series thermistor (B = 4050, T0 = 298.15, nominal
	 * resistance (R0) = 47Kohm).
	 */
	struct thermistor_data_pair data[] = {
		{ 2512 / THERMISTOR_SCALING_FACTOR, 0 },
		{ 2158 / THERMISTOR_SCALING_FACTOR, 10 },
		{ 1772 / THERMISTOR_SCALING_FACTOR, 20 },
		{ 1398 / THERMISTOR_SCALING_FACTOR, 30 },
		{ 1070 / THERMISTOR_SCALING_FACTOR, 40 },
		{ 803 / THERMISTOR_SCALING_FACTOR, 50 },
		{ 597 / THERMISTOR_SCALING_FACTOR, 60 },
		{ 443 / THERMISTOR_SCALING_FACTOR, 70 },
		{ 329 / THERMISTOR_SCALING_FACTOR, 80 },
		{ 247 / THERMISTOR_SCALING_FACTOR, 90 },
		{ 188 / THERMISTOR_SCALING_FACTOR, 100 },
	};
	struct thermistor_info info = {
		.scaling_factor = THERMISTOR_SCALING_FACTOR,
		.num_pairs = ARRAY_SIZE(data),
		.data = data,
	};
	/*
	 * Reference data points to compare accuracy, taken from same set
	 * of derived values but at temp - 1, temp + 1, and in between.
	 */
	struct {
		uint16_t mv; /* not scaled */
		int temp;
	} cmp[] = {
		{ 3030, 1 },  { 2341, 5 },  { 2195, 9 },  { 2120, 11 },
		{ 1966, 15 }, { 1811, 19 }, { 1733, 21 }, { 1581, 25 },
		{ 1434, 29 }, { 1363, 31 }, { 1227, 35 }, { 1100, 39 },
		{ 1040, 41 }, { 929, 45 },  { 827, 49 },  { 780, 51 },
		{ 693, 55 },  { 615, 59 },  { 579, 61 },  { 514, 65 },
		{ 460, 69 },  { 430, 71 },  { 382, 75 },  { 339, 79 },
		{ 320, 81 },  { 285, 85 },  { 254, 89 },  { 240, 91 },
		{ 214, 95 },  { 192, 99 },
	};

	/* Return lowest temperature in data set if voltage is too high. */
	mv = (data[0].mv * info.scaling_factor) + 1;
	t = thermistor_linear_interpolate(mv, &info);
	TEST_ASSERT(t == data[0].temp);

	/* Return highest temperature in data set if voltage is too low. */
	mv = (data[info.num_pairs - 1].mv * info.scaling_factor) - 1;
	t = thermistor_linear_interpolate(mv, &info);
	TEST_ASSERT(t == data[info.num_pairs - 1].temp);

	/* Simple line test */
	for (mv = line_data[0].mv; mv > line_data[line_info.num_pairs - 1].mv;
	     mv--) {
		t = thermistor_linear_interpolate(mv, &line_info);
		TEST_ASSERT(mv == line_data[line_info.num_pairs - 1].temp - t);
	}

	/*
	 * Verify that calculated temperature monotonically
	 * decreases with increase in voltage (0-5V, 10mV steps).
	 */
	for (mv = data[0].mv * info.scaling_factor, t0 = data[0].temp;
	     mv > data[info.num_pairs - 1].mv; mv -= 10) {
		int t1 = thermistor_linear_interpolate(mv, &info);

		TEST_ASSERT(t1 >= t0);
		t0 = t1;
	}

	/* Verify against modelled data, +/- 1C due to scaling. */
	for (i = 0; i < info.num_pairs; i++) {
		mv = data[i].mv * info.scaling_factor;

		t = thermistor_linear_interpolate(mv, &info);
		TEST_ASSERT(t >= data[i].temp - 1 && t <= data[i].temp + 1);
	}

	/*
	 * Verify data points that are interpolated by algorithm, allowing
	 * 1C of inaccuracy.
	 */
	for (i = 0; i < ARRAY_SIZE(cmp); i++) {
		t = thermistor_linear_interpolate(cmp[i].mv, &info);
		TEST_ASSERT(t >= cmp[i].temp - 1 && t <= cmp[i].temp + 1);
	}

	return EC_SUCCESS;
}

void run_test(int argc, const char **argv)
{
	RUN_TEST(test_init_val);
	RUN_TEST(test_sensors_can_be_read);
	RUN_TEST(test_one_fan);
	RUN_TEST(test_two_fans);
	RUN_TEST(test_all_fans);

	RUN_TEST(test_one_limit);
	RUN_TEST(test_several_limits);

	RUN_TEST(test_ncp15wb_adc_to_temp);
	RUN_TEST(test_thermistor_linear_interpolate);
	test_print_result();
}
