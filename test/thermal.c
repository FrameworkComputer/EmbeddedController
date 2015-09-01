/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Test thermal engine.
 */

#include "common.h"
#include "console.h"
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

int dummy_temp_get_val(int idx, int *temp_ptr)
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
	set_temps(-1, -1 , -1, -1);

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
	sleep(2);

	TEST_ASSERT(host_throttled == 0);
	TEST_ASSERT(cpu_throttled == 0);
	TEST_ASSERT(cpu_shutdown == 0);
	TEST_ASSERT(fan_pct == 0);
	TEST_ASSERT(no_temps_read);

	sleep(2);

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

	sleep(2);

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
	sleep(2);
	TEST_ASSERT(fan_pct == 0);

	all_temps(100);
	sleep(2);
	TEST_ASSERT(fan_pct == 0);

	all_temps(101);
	sleep(2);
	TEST_ASSERT(fan_pct == 1);

	all_temps(130);
	sleep(2);
	TEST_ASSERT(fan_pct == 30);

	all_temps(150);
	sleep(2);
	TEST_ASSERT(fan_pct == 50);

	all_temps(170);
	sleep(2);
	TEST_ASSERT(fan_pct == 70);

	all_temps(200);
	sleep(2);
	TEST_ASSERT(fan_pct == 100);

	all_temps(300);
	sleep(2);
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
	sleep(2);
	TEST_ASSERT(fan_pct == 0);

	all_temps(100);
	sleep(2);
	TEST_ASSERT(fan_pct == 0);

	all_temps(101);
	sleep(2);
	TEST_ASSERT(fan_pct == 1);

	all_temps(130);
	sleep(2);
	/* fan 2 is still higher */
	TEST_ASSERT(fan_pct == 30);

	all_temps(150);
	sleep(2);
	/* now fan 1 is higher: 150 = 75% of [120-160] */
	TEST_ASSERT(fan_pct == 75);

	all_temps(170);
	sleep(2);
	/* fan 1 is maxed now */
	TEST_ASSERT(fan_pct == 100);

	all_temps(200);
	sleep(2);
	TEST_ASSERT(fan_pct == 100);

	all_temps(300);
	sleep(2);
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
	sleep(2);
	TEST_ASSERT(fan_pct == 0);

	/* Each sensor has its own range */
	set_temps(40, 0, 0, 0);
	sleep(2);
	TEST_ASSERT(fan_pct == 50);

	set_temps(0, 140, 0, 0);
	sleep(2);
	TEST_ASSERT(fan_pct == 50);

	set_temps(0, 0, 150, 0);
	sleep(2);
	TEST_ASSERT(fan_pct == 50);

	set_temps(0, 0, 0, 400);
	sleep(2);
	TEST_ASSERT(fan_pct == 50);

	set_temps(60, 0, 0, 0);
	sleep(2);
	TEST_ASSERT(fan_pct == 100);

	set_temps(0, 160, 0, 0);
	sleep(2);
	TEST_ASSERT(fan_pct == 100);

	set_temps(0, 0, 200, 0);
	sleep(2);
	TEST_ASSERT(fan_pct == 100);

	set_temps(0, 0, 0, 500);
	sleep(2);
	TEST_ASSERT(fan_pct == 100);

	/* But sensor 0 needs the most cooling */
	all_temps(20);
	sleep(2);
	TEST_ASSERT(fan_pct == 0);

	all_temps(21);
	sleep(2);
	TEST_ASSERT(fan_pct == 2);

	all_temps(30);
	sleep(2);
	TEST_ASSERT(fan_pct == 25);

	all_temps(40);
	sleep(2);
	TEST_ASSERT(fan_pct == 50);

	all_temps(50);
	sleep(2);
	TEST_ASSERT(fan_pct == 75);

	all_temps(60);
	sleep(2);
	TEST_ASSERT(fan_pct == 100);

	all_temps(65);
	sleep(2);
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
	sleep(2);
	TEST_ASSERT(host_throttled == 0);
	TEST_ASSERT(cpu_throttled == 0);
	TEST_ASSERT(cpu_shutdown == 0);

	all_temps(100);
	sleep(2);
	TEST_ASSERT(host_throttled == 0);
	TEST_ASSERT(cpu_throttled == 0);
	TEST_ASSERT(cpu_shutdown == 0);

	all_temps(101);
	sleep(2);
	TEST_ASSERT(host_throttled == 1);
	TEST_ASSERT(cpu_throttled == 0);
	TEST_ASSERT(cpu_shutdown == 0);

	all_temps(100);
	sleep(2);
	TEST_ASSERT(host_throttled == 1);
	TEST_ASSERT(cpu_throttled == 0);
	TEST_ASSERT(cpu_shutdown == 0);

	all_temps(99);
	sleep(2);
	TEST_ASSERT(host_throttled == 0);
	TEST_ASSERT(cpu_throttled == 0);
	TEST_ASSERT(cpu_shutdown == 0);

	all_temps(199);
	sleep(2);
	TEST_ASSERT(host_throttled == 1);
	TEST_ASSERT(cpu_throttled == 0);
	TEST_ASSERT(cpu_shutdown == 0);

	all_temps(200);
	sleep(2);
	TEST_ASSERT(host_throttled == 1);
	TEST_ASSERT(cpu_throttled == 0);
	TEST_ASSERT(cpu_shutdown == 0);

	all_temps(201);
	sleep(2);
	TEST_ASSERT(host_throttled == 1);
	TEST_ASSERT(cpu_throttled == 1);
	TEST_ASSERT(cpu_shutdown == 0);

	all_temps(200);
	sleep(2);
	TEST_ASSERT(host_throttled == 1);
	TEST_ASSERT(cpu_throttled == 1);
	TEST_ASSERT(cpu_shutdown == 0);

	all_temps(199);
	sleep(2);
	TEST_ASSERT(host_throttled == 1);
	TEST_ASSERT(cpu_throttled == 0);
	TEST_ASSERT(cpu_shutdown == 0);

	all_temps(99);
	sleep(2);
	TEST_ASSERT(host_throttled == 0);
	TEST_ASSERT(cpu_throttled == 0);
	TEST_ASSERT(cpu_shutdown == 0);

	all_temps(201);
	sleep(2);
	TEST_ASSERT(host_throttled == 1);
	TEST_ASSERT(cpu_throttled == 1);
	TEST_ASSERT(cpu_shutdown == 0);

	all_temps(99);
	sleep(2);
	TEST_ASSERT(host_throttled == 0);
	TEST_ASSERT(cpu_throttled == 0);
	TEST_ASSERT(cpu_shutdown == 0);

	all_temps(301);
	sleep(2);
	TEST_ASSERT(host_throttled == 1);
	TEST_ASSERT(cpu_throttled == 1);
	TEST_ASSERT(cpu_shutdown == 1);

	/* We probably won't be able to read the CPU temp while shutdown,
	 * so nothing will change. */
	all_temps(-1);
	sleep(2);
	TEST_ASSERT(host_throttled == 1);
	TEST_ASSERT(cpu_throttled == 1);
	/* cpu_shutdown is only set for testing purposes. The thermal task
	 * doesn't do anything that could clear it. */

	all_temps(50);
	sleep(2);
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
	sleep(2);
	TEST_ASSERT(host_throttled == 1); /* 1=low, 2=warn, 3=low */
	TEST_ASSERT(cpu_throttled == 0);
	TEST_ASSERT(cpu_shutdown == 0);

	set_temps(500, 50, -1, 10);	/* 1=low, 2=X, 3=low */
	sleep(2);
	TEST_ASSERT(host_throttled == 0);
	TEST_ASSERT(cpu_throttled == 0);
	TEST_ASSERT(cpu_shutdown == 0);

	set_temps(500, 170, 210, 10);	/* 1=warn, 2=high, 3=low */
	sleep(2);
	TEST_ASSERT(host_throttled == 1);
	TEST_ASSERT(cpu_throttled == 1);
	TEST_ASSERT(cpu_shutdown == 0);

	set_temps(500, 100, 50, 40);	/* 1=low, 2=low, 3=high */
	sleep(2);
	TEST_ASSERT(host_throttled == 1);
	TEST_ASSERT(cpu_throttled == 1);
	TEST_ASSERT(cpu_shutdown == 0);

	set_temps(500, 100, 50, 41);	/* 1=low, 2=low, 3=shutdown */
	sleep(2);
	TEST_ASSERT(host_throttled == 1);
	TEST_ASSERT(cpu_throttled == 1);
	TEST_ASSERT(cpu_shutdown == 1);

	all_temps(0);			/* reset from shutdown */
	sleep(2);
	TEST_ASSERT(host_throttled == 0);
	TEST_ASSERT(cpu_throttled == 0);


	return EC_SUCCESS;
}

/* Tests for ncp15wb thermistor ADC-to-temp calculation */
#define LOW_ADC_TEST_VALUE	887 /* 0 C */
#define HIGH_ADC_TEST_VALUE	100 /* > 100C */

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
		{ 615, 30 },
		{ 561, 35 },
		{ 508, 40 },
		{ 407, 50 },
		{ 315, 60 },
		{ 243, 70 },
		{ 186, 80 },
		{ 140, 90 },
		{ 107, 100 },
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
		TEST_ASSERT(new_temp == temp ||
			    new_temp == temp + 1);
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


void run_test(void)
{
	RUN_TEST(test_init_val);
	RUN_TEST(test_sensors_can_be_read);
	RUN_TEST(test_one_fan);
	RUN_TEST(test_two_fans);
	RUN_TEST(test_all_fans);

	RUN_TEST(test_one_limit);
	RUN_TEST(test_several_limits);

	RUN_TEST(test_ncp15wb_adc_to_temp);
	test_print_result();
}
