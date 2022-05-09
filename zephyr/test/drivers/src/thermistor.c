/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/zephyr.h>
#include <ztest.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/adc/adc_emul.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <temp_sensor.h>

#include "common.h"
#include "../driver/temp_sensor/thermistor.h"
#include "temp_sensor/temp_sensor.h"
#include "test/drivers/test_state.h"


#define GPIO_PG_EC_DSW_PWROK_PATH DT_PATH(named_gpios, pg_ec_dsw_pwrok)
#define GPIO_PG_EC_DSW_PWROK_PORT DT_GPIO_PIN(GPIO_PG_EC_DSW_PWROK_PATH, gpios)

#define ADC_DEVICE_NODE		DT_NODELABEL(adc0)

/* TODO replace counting macros with DT macro when
 * https://github.com/zephyrproject-rtos/zephyr/issues/38715 lands
 */
#define _ACCUMULATOR(x)
#define NAMED_TEMP_SENSORS_SIZE                                     \
	DT_FOREACH_CHILD(DT_PATH(named_temp_sensors), _ACCUMULATOR) \
	0
#define TEMP_SENSORS_ENABLED_SIZE \
	DT_FOREACH_STATUS_OKAY(cros_ec_temp_sensor, _ACCUMULATOR) 0

/* Conversion of temperature doesn't need to be 100% accurate */
#define TEMP_EPS	2

#define A_VALID_VOLTAGE 1000
/**
 * Test if get temp function return expected error when ADC is not powered
 * (indicated as GPIO pin set to low) and return success after powering on ADC.
 */
ZTEST_USER(thermistor, test_thermistor_power_pin)
{
	int temp;
	int sensor_idx;

	const struct device *gpio_dev =
		DEVICE_DT_GET(DT_GPIO_CTLR(GPIO_PG_EC_DSW_PWROK_PATH, gpios));
	const struct device *adc_dev = DEVICE_DT_GET(ADC_DEVICE_NODE);

	zassert_not_null(gpio_dev, "Cannot get GPIO device");
	zassert_not_null(adc_dev, "Cannot get ADC device");

	/* Make sure that ADC return a valid value */
	for (sensor_idx = 0; sensor_idx < NAMED_TEMP_SENSORS_SIZE;
	     sensor_idx++) {
		const struct temp_sensor_t *sensor = &temp_sensors[sensor_idx];

		zassert_ok(adc_emul_const_value_set(adc_dev,
						   sensor->idx,
						   A_VALID_VOLTAGE),
			   "adc_emul_value_func_set() failed on %s",
			   sensor->name);
	}

	/* pg_ec_dsw_pwrok = 0 means ADC is not powered. */
	zassert_ok(gpio_emul_input_set(gpio_dev, GPIO_PG_EC_DSW_PWROK_PORT, 0),
		   NULL);

	for (sensor_idx = 0; sensor_idx < NAMED_TEMP_SENSORS_SIZE;
	     sensor_idx++) {
		const struct temp_sensor_t *sensor = &temp_sensors[sensor_idx];

		zassert_equal(EC_ERROR_NOT_POWERED,
			      sensor->zephyr_info->read(sensor, &temp),
			      "%s failed", sensor->name);
	}

	/* pg_ec_dsw_pwrok = 1 means ADC is powered. */
	zassert_ok(gpio_emul_input_set(gpio_dev, GPIO_PG_EC_DSW_PWROK_PORT, 1),
		   NULL);

	for (sensor_idx = 0; sensor_idx < NAMED_TEMP_SENSORS_SIZE;
	     sensor_idx++) {
		const struct temp_sensor_t *sensor = &temp_sensors[sensor_idx];

		zassert_equal(EC_SUCCESS,
			      sensor->zephyr_info->read(sensor, &temp),
			      "%s failed", sensor->name);
	}
}

/* Simple ADC emulator custom function which always return error */
static int adc_error_func(const struct device *dev, unsigned int channel,
			  void *param, uint32_t *result)
{
	return -EINVAL;
}

/** Test if get temp function return expected error on ADC malfunction */
ZTEST_USER(thermistor, test_thermistor_adc_read_error)
{
	int temp;
	int sensor_idx;

	const struct device *adc_dev = DEVICE_DT_GET(ADC_DEVICE_NODE);

	zassert_not_null(adc_dev, "Cannot get ADC device");

	/* Return error on all ADC channels */
	for (sensor_idx = 0; sensor_idx < NAMED_TEMP_SENSORS_SIZE;
	     sensor_idx++) {
		const struct temp_sensor_t *sensor = &temp_sensors[sensor_idx];

		zassert_ok(adc_emul_value_func_set(adc_dev, sensor->idx,
						   adc_error_func, NULL),
			   "adc_emul_value_func_set() failed on %s",
			   sensor->name);
	}

	for (sensor_idx = 0; sensor_idx < NAMED_TEMP_SENSORS_SIZE;
	     sensor_idx++) {
		const struct temp_sensor_t *sensor = &temp_sensors[sensor_idx];

		zassert_equal(EC_ERROR_UNKNOWN,
			      sensor->zephyr_info->read(sensor, &temp),
			      "%s failed", sensor->name);
	}
}

/** Get resistance of thermistor for given temperature */
static int resistance_47kohm_B4050(int t)
{
	/* Thermistor manufacturer resistance lookup table*/
	int r_table[] = {
		155700, 147900, 140600, 133700, 127200, /* 0*C  - 4*C */
		121000, 115100, 109600, 104300, 99310,  /* 5*C  - 9*C */
		94600,  90130,  85890,  81870,  78070,  /* 10*C - 14*C */
		74450,  71020,  67770,  64680,  61750,  /* 15*C - 19*C */
		58970,  56320,  53810,  51430,  49160,  /* 20*C - 24*C */
		47000,  44950,  42990,  41130,  39360,  /* 25*C - 29*C */
		37680,  36070,  34540,  33080,  31690,  /* 30*C - 34*C */
		30360,  29100,  27900,  26750,  25650,  /* 35*C - 39*C */
		24610,  23610,  22660,  21750,  20880,  /* 40*C - 44*C */
		20050,  19260,  18500,  17780,  17090,  /* 45*C - 49*C */
		16430,  15800,  15200,  14620,  14070,  /* 50*C - 54*C */
		13540,  13030,  12550,  12090,  11640,  /* 55*C - 59*C */
		11210,  10800,  10410,  10040,  9676,   /* 60*C - 64*C */
		9331,   8999,   8680,   8374,   8081,   /* 65*C - 69*C */
		7799,   7528,   7268,   7018,   6777,   /* 70*C - 74*C */
		6546,   6324,   6111,   5906,   5708,   /* 75*C - 79*C */
		5518,   5335,   5160,   4990,   4827,   /* 80*C - 84*C */
		4671,   4519,   4374,   4233,   4098,   /* 85*C - 89*C */
		3968,   3842,   3721,   3605,   3492,   /* 90*C - 94*C */
		3384,   3279,   3179,   3082,   2988,   /* 95*C - 99*C */
		2898                                    /* 100*C */
	};

	t -= 273;
	if (t < 0)
		return r_table[0] + 10000;

	if (t >= ARRAY_SIZE(r_table))
		return r_table[ARRAY_SIZE(r_table) - 1] - 100;

	return r_table[t];
}

/**
 * Calculate output voltage in voltage divider circuit using formula
 * Vout = Vs * r2 / (r1 + r2)
 */
static int volt_divider(int vs, int r1, int r2)
{
	return vs * r2 / (r1 + r2);
}

struct thermistor_state {
	const int v;
	const int r;
	int temp_expected;
};

/** ADC emulator function which calculate output voltage for given thermistor */
static int adc_temperature_func(const struct device *dev, unsigned int channel,
				void *param, uint32_t *result)
{
	struct thermistor_state *s = (struct thermistor_state *)param;

	*result = volt_divider(s->v,
			       s->r,
			       resistance_47kohm_B4050(s->temp_expected));

	return 0;
}

/** Test conversion from ADC raw value to temperature */
static void do_thermistor_test(const struct temp_sensor_t *temp_sensor,
			       int reference_mv, int reference_ohms)
{
	int temp_expected;
	int temp;

	const struct device *adc_dev = DEVICE_DT_GET(ADC_DEVICE_NODE);
	struct thermistor_state state = {
		.v = reference_mv,
		.r = reference_ohms,
	};

	zassert_not_null(adc_dev, "Cannot get ADC device");

	/* Setup ADC channel */
	zassert_ok(adc_emul_value_func_set(adc_dev,
					   temp_sensor->idx,
					   adc_temperature_func, &state),
		   "adc_emul_value_func_set() failed on %s", temp_sensor->name);

	/* Makes sure that reference voltage is correct for given thermistor */
	zassert_ok(adc_emul_ref_voltage_set(adc_dev, ADC_REF_INTERNAL, state.v),
		   "adc_emul_ref_voltage_set() failed %s on ",
		   temp_sensor->name);

	/* Test whole supported range from 0*C to 100*C (273*K to 373*K) */
	for (temp_expected = 273; temp_expected <= 373; temp_expected++) {
		state.temp_expected = temp_expected;
		zassert_equal(EC_SUCCESS,
			temp_sensor->zephyr_info->read(temp_sensor, &temp),
			"failed on %s", temp_sensor->name);
		zassert_within(temp_expected, temp, TEMP_EPS,
			       "Expected %d*K, got %d*K on %s", temp_expected,
			       temp, temp_sensor->name);
	}

	/* Temperatures below 0*C should be reported as 0*C */
	state.temp_expected = -15 + 273;
	zassert_equal(EC_SUCCESS,
		      temp_sensor->zephyr_info->read(temp_sensor, &temp),
		      "failed on %s", temp_sensor->name);
	zassert_equal(273, temp, "Expected %d*K, got %d*K on %s", 273, temp,
		      temp_sensor->name);

	/* Temperatures above 100*C should be reported as 100*C */
	state.temp_expected = 115 + 273;
	zassert_equal(EC_SUCCESS,
		      temp_sensor->zephyr_info->read(temp_sensor, &temp),
		      "failed on %s", temp_sensor->name);
	zassert_equal(373, temp, "Expected %d*K, got %d*K on %s", 373, temp,
		      temp_sensor->name);
}

#define GET_THERMISTOR_REF_MV(node_id)             \
	[ZSHIM_TEMP_SENSOR_ID(node_id)] = DT_PROP( \
		DT_PHANDLE(node_id, thermistor), steinhart_reference_mv),

#define GET_THERMISTOR_REF_RES(node_id)            \
	[ZSHIM_TEMP_SENSOR_ID(node_id)] = DT_PROP( \
		DT_PHANDLE(node_id, thermistor), steinhart_reference_res),

ZTEST_USER(thermistor, test_thermistors_adc_temperature_conversion)
{
	int sensor_idx;

	const static int reference_mv_arr[] = { DT_FOREACH_STATUS_OKAY(
		cros_temp_sensor, GET_THERMISTOR_REF_MV) };
	const static int reference_res_arr[] = { DT_FOREACH_STATUS_OKAY(
		cros_temp_sensor, GET_THERMISTOR_REF_RES) };

	for (sensor_idx = 0; sensor_idx < NAMED_TEMP_SENSORS_SIZE; sensor_idx++)
		do_thermistor_test(&temp_sensors[sensor_idx],
				   reference_mv_arr[sensor_idx],
				   reference_res_arr[sensor_idx]);
}

ZTEST_USER(thermistor, test_device_nodes_enabled)
{
	zassert_equal(NAMED_TEMP_SENSORS_SIZE, TEMP_SENSORS_ENABLED_SIZE,
		      "Temperature sensors in device tree and "
		      "those enabled for test differ");

	/* Thermistor nodes being enabled are already tested by compilation. */
}

static void *thermistor_setup(void)
{
	const struct device *dev =
		DEVICE_DT_GET(DT_GPIO_CTLR(GPIO_PG_EC_DSW_PWROK_PATH, gpios));

	zassert_not_null(dev, NULL);
	/* Before tests make sure that power pin is set. */
	zassert_ok(gpio_emul_input_set(dev, GPIO_PG_EC_DSW_PWROK_PORT, 1),
		   NULL);

	return NULL;
}

ZTEST_SUITE(thermistor, drivers_predicate_post_main, thermistor_setup, NULL,
	    NULL, NULL);
