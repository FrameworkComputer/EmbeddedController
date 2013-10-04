/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Test thermal engine.
 */

#include "battery.h"
#include "battery_smart.h"
#include "chipset.h"
#include "common.h"
#include "console.h"
#include "extpower.h"
#include "extpower_falco.h"
#include "fan.h"
#include "hooks.h"
#include "host_command.h"
#include "printf.h"
#include "temp_sensor.h"
#include "test_util.h"
#include "thermal.h"
#include "throttle_ap.h"
#include "timer.h"
#include "util.h"

/* Normally private stuff from the modules we're going to test */
#include "thermal_falco_externs.h"


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
static int mock_id;
static int mock_ac;
static int mock_charger_current;
static int mock_battery_discharge_current;

/* constants to match against throttling sources */
static const int t_s_therm = (1 << THROTTLE_SRC_THERMAL);
static const int t_s_power = (1 << THROTTLE_SRC_POWER);
static const int t_s_both = ((1 << THROTTLE_SRC_THERMAL) |
			     (1 << THROTTLE_SRC_POWER));

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

void pwm_fan_set_percent_needed(int pct)
{
	fan_pct = pct;
}

void smi_sensor_failure_warning(void)
{
	no_temps_read = 1;
}

static void change_ac(int val)
{
	mock_ac = val;
	extpower_interrupt(GPIO_AC_PRESENT);
	sleep(1);
}

int gpio_get_level(enum gpio_signal signal)
{
	if (signal == GPIO_AC_PRESENT)
		return mock_ac;
	return 0;
}

int adc_read_channel(enum adc_channel ch)
{
	switch (ch) {
	case ADC_AC_ADAPTER_ID_VOLTAGE:
		return mock_id;
	case ADC_CH_CHARGER_CURRENT:
		return mock_charger_current;
	default:
		break;
	}

	return 0;
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

static void reset_mock_battery(void)
{
	const struct battery_info *bat_info = battery_get_info();

	/* 50% of charge */
	sb_write(SB_RELATIVE_STATE_OF_CHARGE, 50);
	sb_write(SB_ABSOLUTE_STATE_OF_CHARGE, 50);
	/* 25 degree Celsius */
	sb_write(SB_TEMPERATURE, 250 + 2731);
	/* Normal voltage */
	sb_write(SB_VOLTAGE, bat_info->voltage_normal);
	sb_write(SB_CHARGING_VOLTAGE, bat_info->voltage_max);
	sb_write(SB_CHARGING_CURRENT, 4000);
	/* Discharging at 100mAh */
	sb_write(SB_CURRENT, -100);
}
DECLARE_HOOK(HOOK_INIT, reset_mock_battery, HOOK_PRIO_DEFAULT);

static void mock_batt(int cur)
{
	sb_write(SB_CURRENT, -cur);	/* discharge current is neg here */
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

	/* other mocked inputs */
	mock_id = 0;
	mock_ac = 0;
	mock_charger_current = 0;
	mock_battery_discharge_current = 0;
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
	TEST_ASSERT(host_throttled == t_s_therm);
	TEST_ASSERT(cpu_throttled == 0);
	TEST_ASSERT(cpu_shutdown == 0);

	all_temps(100);
	sleep(2);
	TEST_ASSERT(host_throttled == t_s_therm);
	TEST_ASSERT(cpu_throttled == 0);
	TEST_ASSERT(cpu_shutdown == 0);

	all_temps(99);
	sleep(2);
	TEST_ASSERT(host_throttled == 0);
	TEST_ASSERT(cpu_throttled == 0);
	TEST_ASSERT(cpu_shutdown == 0);

	all_temps(199);
	sleep(2);
	TEST_ASSERT(host_throttled == t_s_therm);
	TEST_ASSERT(cpu_throttled == 0);
	TEST_ASSERT(cpu_shutdown == 0);

	all_temps(200);
	sleep(2);
	TEST_ASSERT(host_throttled == t_s_therm);
	TEST_ASSERT(cpu_throttled == 0);
	TEST_ASSERT(cpu_shutdown == 0);

	all_temps(201);
	sleep(2);
	TEST_ASSERT(host_throttled == t_s_therm);
	TEST_ASSERT(cpu_throttled == t_s_therm);
	TEST_ASSERT(cpu_shutdown == 0);

	all_temps(200);
	sleep(2);
	TEST_ASSERT(host_throttled == t_s_therm);
	TEST_ASSERT(cpu_throttled == t_s_therm);
	TEST_ASSERT(cpu_shutdown == 0);

	all_temps(199);
	sleep(2);
	TEST_ASSERT(host_throttled == t_s_therm);
	TEST_ASSERT(cpu_throttled == 0);
	TEST_ASSERT(cpu_shutdown == 0);

	all_temps(99);
	sleep(2);
	TEST_ASSERT(host_throttled == 0);
	TEST_ASSERT(cpu_throttled == 0);
	TEST_ASSERT(cpu_shutdown == 0);

	all_temps(201);
	sleep(2);
	TEST_ASSERT(host_throttled == t_s_therm);
	TEST_ASSERT(cpu_throttled == t_s_therm);
	TEST_ASSERT(cpu_shutdown == 0);

	all_temps(99);
	sleep(2);
	TEST_ASSERT(host_throttled == 0);
	TEST_ASSERT(cpu_throttled == 0);
	TEST_ASSERT(cpu_shutdown == 0);

	all_temps(301);
	sleep(2);
	TEST_ASSERT(host_throttled == t_s_therm);
	TEST_ASSERT(cpu_throttled == t_s_therm);
	TEST_ASSERT(cpu_shutdown == 1);

	/* We probably won't be able to read the CPU temp while shutdown,
	 * so nothing will change. */
	all_temps(-1);
	sleep(2);
	TEST_ASSERT(host_throttled == t_s_therm);
	TEST_ASSERT(cpu_throttled == t_s_therm);
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
	TEST_ASSERT(host_throttled == t_s_therm); /* 1=low, 2=warn, 3=low */
	TEST_ASSERT(cpu_throttled == 0);
	TEST_ASSERT(cpu_shutdown == 0);

	set_temps(500, 50, -1, 10);	/* 1=low, 2=X, 3=low */
	sleep(2);
	TEST_ASSERT(host_throttled == 0);
	TEST_ASSERT(cpu_throttled == 0);
	TEST_ASSERT(cpu_shutdown == 0);

	set_temps(500, 170, 210, 10);	/* 1=warn, 2=high, 3=low */
	sleep(2);
	TEST_ASSERT(host_throttled == t_s_therm);
	TEST_ASSERT(cpu_throttled == t_s_therm);
	TEST_ASSERT(cpu_shutdown == 0);

	set_temps(500, 100, 50, 40);	/* 1=low, 2=low, 3=high */
	sleep(2);
	TEST_ASSERT(host_throttled == t_s_therm);
	TEST_ASSERT(cpu_throttled == t_s_therm);
	TEST_ASSERT(cpu_shutdown == 0);

	set_temps(500, 100, 50, 41);	/* 1=low, 2=low, 3=shutdown */
	sleep(2);
	TEST_ASSERT(host_throttled == t_s_therm);
	TEST_ASSERT(cpu_throttled == t_s_therm);
	TEST_ASSERT(cpu_shutdown == 1);

	all_temps(0);			/* reset from shutdown */
	sleep(2);
	TEST_ASSERT(host_throttled == 0);
	TEST_ASSERT(cpu_throttled == 0);


	return EC_SUCCESS;
}

static int test_batt(void)
{
	struct adapter_limits *l;
	int longtime;
	int i;

	reset_mocks();
	/* We're assuming two limits, mild and urgent. */
	TEST_ASSERT(NUM_BATT_THRESHOLDS == 2);
	/* Find out which is which, only use the lower one */
	if (batt_limits[0].hi_val > batt_limits[1].hi_val)
		l = &batt_limits[1];
	else
		l = &batt_limits[0];

	/* Find a time longer than all sample count limits */
	for (i = longtime = 0; i < NUM_BATT_THRESHOLDS; i++)
		longtime = MAX(longtime,
			       MAX(batt_limits[i].lo_cnt,
				   batt_limits[i].hi_cnt));
	longtime += 2;

	/* On AC, but this doesn't actually matter for this test */
	mock_batt(0);
	change_ac(1);

	TEST_ASSERT(ap_is_throttled == 0);
	change_ac(0);
	TEST_ASSERT(ap_is_throttled == 0);

	/* reset, by staying low for a long time */
	usleep(EXTPOWER_FALCO_POLL_PERIOD * longtime);
	TEST_ASSERT(l->triggered == 0);
	TEST_ASSERT(ap_is_throttled == 0);

	/* mock_batt() specifies the DISCHARGE current. Charging
	 * should do nothing, no matter how high. */
	mock_batt(-1);
	usleep(EXTPOWER_FALCO_POLL_PERIOD * longtime);
	TEST_ASSERT(l->triggered == 0);
	TEST_ASSERT(ap_is_throttled == 0);

	/* midrange for a long time shouldn't do anything */
	mock_batt((l->lo_val + l->hi_val) / 2);
	usleep(EXTPOWER_FALCO_POLL_PERIOD * longtime);
	TEST_ASSERT(l->triggered == 0);
	TEST_ASSERT(ap_is_throttled == 0);

	/* above high limit for not quite long enough */
	mock_batt(l->hi_val + 1);
	usleep(EXTPOWER_FALCO_POLL_PERIOD * (l->hi_cnt - 1));
	TEST_ASSERT(l->count != 0);
	TEST_ASSERT(l->triggered == 0);
	TEST_ASSERT(ap_is_throttled == 0);

	/* drop below the high limit once */
	mock_batt(l->hi_val - 1);
	usleep(EXTPOWER_FALCO_POLL_PERIOD * 1);
	TEST_ASSERT(l->count == 0);
	TEST_ASSERT(l->triggered == 0);
	TEST_ASSERT(ap_is_throttled == 0);

	/* now back up */
	mock_batt(l->hi_val + 1);
	usleep(EXTPOWER_FALCO_POLL_PERIOD * (l->hi_cnt - 1));
	TEST_ASSERT(l->count != 0);
	TEST_ASSERT(l->triggered == 0);
	TEST_ASSERT(ap_is_throttled == 0);

	/* one more ought to do it */
	usleep(EXTPOWER_FALCO_POLL_PERIOD * 1);
	TEST_ASSERT(l->triggered == 1);
	TEST_ASSERT(ap_is_throttled);

	/* going midrange for a long time shouldn't change anything */
	mock_batt((l->lo_val + l->hi_val) / 2);
	usleep(EXTPOWER_FALCO_POLL_PERIOD * longtime);
	TEST_ASSERT(l->triggered == 1);
	TEST_ASSERT(ap_is_throttled);

	/* charge for not quite long enough */
	mock_batt(-1);
	usleep(EXTPOWER_FALCO_POLL_PERIOD * (l->lo_cnt - 1));
	TEST_ASSERT(l->triggered == 1);
	TEST_ASSERT(ap_is_throttled);

	/* back above the low limit once */
	mock_batt(l->lo_val + 1);
	usleep(EXTPOWER_FALCO_POLL_PERIOD * 1);
	TEST_ASSERT(l->triggered == 1);
	TEST_ASSERT(ap_is_throttled);

	/* now charge again  - that should have reset the count */
	mock_batt(-1);
	usleep(EXTPOWER_FALCO_POLL_PERIOD * (l->lo_cnt - 1));
	TEST_ASSERT(l->triggered == 1);
	TEST_ASSERT(ap_is_throttled);

	/* One more ought to do it */
	usleep(EXTPOWER_FALCO_POLL_PERIOD * 1);
	TEST_ASSERT(l->triggered == 0);
	TEST_ASSERT(ap_is_throttled == 0);

	return EC_SUCCESS;
}

static int test_several_limits_with_batt(void)
{
	struct adapter_limits *l;
	int longtime;
	int i;

	/* We're assuming two limits, mild and urgent. */
	TEST_ASSERT(NUM_BATT_THRESHOLDS == 2);
	/* Find out which is which, only use the lower one */
	if (batt_limits[0].hi_val > batt_limits[1].hi_val)
		l = &batt_limits[1];
	else
		l = &batt_limits[0];

	/* Find a time longer than all sample count limits */
	for (i = longtime = 0; i < NUM_BATT_THRESHOLDS; i++)
		longtime = MAX(longtime,
			       MAX(batt_limits[i].lo_cnt,
				   batt_limits[i].hi_cnt));
	longtime += 2;
	longtime *= EXTPOWER_FALCO_POLL_PERIOD;

	reset_mocks();

	/* Set some thermal limits */
	thermal_params[1].temp_host[EC_TEMP_THRESH_WARN] = 150;
	thermal_params[1].temp_host[EC_TEMP_THRESH_HIGH] = 200;
	thermal_params[1].temp_host[EC_TEMP_THRESH_HALT] = 250;

	thermal_params[2].temp_host[EC_TEMP_THRESH_WARN] = 100;
	thermal_params[2].temp_host[EC_TEMP_THRESH_HIGH] = 200;
	thermal_params[2].temp_host[EC_TEMP_THRESH_HALT] = 300;

	thermal_params[3].temp_host[EC_TEMP_THRESH_WARN] = 20;
	thermal_params[3].temp_host[EC_TEMP_THRESH_HIGH] = 30;
	thermal_params[3].temp_host[EC_TEMP_THRESH_HALT] = 40;


	/* On AC, charging */
	mock_batt(-1);
	all_temps(0);
	change_ac(1);
	usleep(longtime);
	/* Everything is ready */
	TEST_ASSERT(l->triggered == 0);
	TEST_ASSERT(ap_is_throttled == 0);
	TEST_ASSERT(host_throttled == 0);
	TEST_ASSERT(cpu_throttled == 0);
	TEST_ASSERT(cpu_shutdown == 0);


	set_temps(500, 100, 150, 10);
	usleep(longtime);
	TEST_ASSERT(host_throttled == t_s_therm); /* 1=low, 2=warn, 3=low */
	TEST_ASSERT(cpu_throttled == 0);
	TEST_ASSERT(cpu_shutdown == 0);

	/* battery up and down */
	mock_batt(l->hi_val + 1);
	usleep(longtime);
	TEST_ASSERT(l->triggered == 1);
	TEST_ASSERT(ap_is_throttled);
	TEST_ASSERT(cpu_throttled == 0);
	TEST_ASSERT(host_throttled == t_s_both); /* 1=low, 2=warn, 3=low */
	TEST_ASSERT(cpu_shutdown == 0);
	mock_batt(-1);
	usleep(longtime);
	TEST_ASSERT(l->triggered == 0);
	TEST_ASSERT(ap_is_throttled == 0);
	TEST_ASSERT(host_throttled == t_s_therm); /* 1=low, 2=warn, 3=low */
	TEST_ASSERT(cpu_throttled == 0);
	TEST_ASSERT(cpu_shutdown == 0);


	set_temps(500, 50, -1, 10);	/* 1=low, 2=X, 3=low */
	usleep(longtime);
	TEST_ASSERT(host_throttled == 0);
	TEST_ASSERT(cpu_throttled == 0);
	TEST_ASSERT(cpu_shutdown == 0);
	TEST_ASSERT(l->triggered == 0);
	TEST_ASSERT(ap_is_throttled == 0);

	/* battery up and down */
	mock_batt(l->hi_val + 1);
	usleep(longtime);
	TEST_ASSERT(l->triggered == 1);
	TEST_ASSERT(ap_is_throttled);
	TEST_ASSERT(cpu_throttled == 0);
	TEST_ASSERT(host_throttled == t_s_power);
	TEST_ASSERT(cpu_shutdown == 0);
	mock_batt(-1);
	usleep(longtime);
	TEST_ASSERT(l->triggered == 0);
	TEST_ASSERT(ap_is_throttled == 0);
	TEST_ASSERT(host_throttled == 0);
	TEST_ASSERT(cpu_throttled == 0);
	TEST_ASSERT(cpu_shutdown == 0);


	set_temps(500, 170, 210, 10);	/* 1=warn, 2=high, 3=low */
	sleep(2);
	TEST_ASSERT(host_throttled == t_s_therm);
	TEST_ASSERT(cpu_throttled == t_s_therm);
	TEST_ASSERT(cpu_shutdown == 0);

	/* battery up and down */
	mock_batt(l->hi_val + 1);
	usleep(longtime);
	TEST_ASSERT(l->triggered == 1);
	TEST_ASSERT(ap_is_throttled);
	TEST_ASSERT(cpu_throttled == t_s_therm);
	TEST_ASSERT(host_throttled == t_s_both);
	TEST_ASSERT(cpu_shutdown == 0);
	mock_batt(-1);
	usleep(longtime);
	TEST_ASSERT(l->triggered == 0);
	TEST_ASSERT(ap_is_throttled == 0);
	TEST_ASSERT(host_throttled == t_s_therm);
	TEST_ASSERT(cpu_throttled == t_s_therm);
	TEST_ASSERT(cpu_shutdown == 0);


	set_temps(500, 100, 50, 40);	/* 1=low, 2=low, 3=high */
	usleep(longtime);
	TEST_ASSERT(host_throttled == t_s_therm);
	TEST_ASSERT(cpu_throttled == t_s_therm);
	TEST_ASSERT(cpu_shutdown == 0);

	/* battery up and down */
	mock_batt(l->hi_val + 1);
	usleep(longtime);
	TEST_ASSERT(l->triggered == 1);
	TEST_ASSERT(ap_is_throttled);
	TEST_ASSERT(cpu_throttled == t_s_therm);
	TEST_ASSERT(host_throttled == t_s_both);
	TEST_ASSERT(cpu_shutdown == 0);
	mock_batt(-1);
	usleep(longtime);
	TEST_ASSERT(l->triggered == 0);
	TEST_ASSERT(ap_is_throttled == 0);
	TEST_ASSERT(host_throttled == t_s_therm);
	TEST_ASSERT(cpu_throttled == t_s_therm);
	TEST_ASSERT(cpu_shutdown == 0);


	set_temps(500, 100, 50, 41);	/* 1=low, 2=low, 3=shutdown */
	usleep(longtime);
	TEST_ASSERT(host_throttled == t_s_therm);
	TEST_ASSERT(cpu_throttled == t_s_therm);
	TEST_ASSERT(cpu_shutdown == 1);

	/* battery up and down */
	mock_batt(l->hi_val + 1);
	usleep(longtime);
	TEST_ASSERT(l->triggered == 1);
	TEST_ASSERT(ap_is_throttled);
	TEST_ASSERT(cpu_throttled == t_s_therm);
	TEST_ASSERT(host_throttled == t_s_both);
	TEST_ASSERT(cpu_shutdown == 1);
	mock_batt(-1);
	usleep(longtime);
	TEST_ASSERT(l->triggered == 0);
	TEST_ASSERT(ap_is_throttled == 0);
	TEST_ASSERT(host_throttled == t_s_therm);
	TEST_ASSERT(cpu_throttled == t_s_therm);
	TEST_ASSERT(cpu_shutdown == 1);


	all_temps(0);
	usleep(longtime);
	TEST_ASSERT(host_throttled == 0);
	TEST_ASSERT(cpu_throttled == 0);
	TEST_ASSERT(l->triggered == 0);
	TEST_ASSERT(ap_is_throttled == 0);

	/* battery up and down */
	mock_batt(l->hi_val + 1);
	usleep(longtime);
	TEST_ASSERT(l->triggered == 1);
	TEST_ASSERT(ap_is_throttled);
	TEST_ASSERT(cpu_throttled == 0);
	TEST_ASSERT(host_throttled == t_s_power);
	mock_batt(-1);
	usleep(longtime);
	TEST_ASSERT(l->triggered == 0);
	TEST_ASSERT(ap_is_throttled == 0);
	TEST_ASSERT(host_throttled == 0);
	TEST_ASSERT(cpu_throttled == 0);

	return EC_SUCCESS;
}


void run_test(void)
{
	test_chipset_on();

	RUN_TEST(test_init_val);
	RUN_TEST(test_sensors_can_be_read);
	RUN_TEST(test_one_fan);
	RUN_TEST(test_two_fans);
	RUN_TEST(test_all_fans);

	RUN_TEST(test_one_limit);
	RUN_TEST(test_several_limits);

	RUN_TEST(test_batt);
	RUN_TEST(test_several_limits_with_batt);

	test_print_result();
}
