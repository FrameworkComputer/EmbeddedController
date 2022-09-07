/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/adc/adc_emul.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/gpio/gpio_emul.h>

#include <math.h>

#include "common.h"
#include "temp_sensor.h"
#include "temp_sensor/temp_sensor.h"
#include "test/drivers/test_state.h"

#define GPIO_PG_EC_DSW_PWROK_PATH DT_PATH(named_gpios, pg_ec_dsw_pwrok)
#define GPIO_PG_EC_DSW_PWROK_PORT DT_GPIO_PIN(GPIO_PG_EC_DSW_PWROK_PATH, gpios)

#define GPIO_EC_PG_PIN_TEMP_PATH DT_PATH(named_gpios, ec_pg_pin_temp)
#define GPIO_EC_PG_PIN_TEMP_PORT DT_GPIO_PIN(GPIO_EC_PG_PIN_TEMP_PATH, gpios)

#define ADC_DEVICE_NODE DT_NODELABEL(adc0)
#define ADC_CHANNELS_NUM DT_PROP(DT_NODELABEL(adc0), nchannels)

/** Test error code when invalid sensor is passed to temp_sensor_read() */
ZTEST_USER(temp_sensor, test_temp_sensor_wrong_id)
{
	int temp;

	zassert_equal(EC_ERROR_INVAL,
		      temp_sensor_read(TEMP_SENSOR_COUNT, &temp), NULL);
}

/** Test error code when temp_sensor_read() is called with powered off ADC */
ZTEST_USER(temp_sensor, test_temp_sensor_adc_error)
{
	const struct device *gpio_dev =
		DEVICE_DT_GET(DT_GPIO_CTLR(GPIO_PG_EC_DSW_PWROK_PATH, gpios));
	int temp;

	zassert_not_null(gpio_dev, "Cannot get GPIO device");

	/*
	 * pg_ec_dsw_pwrok = 0 means ADC is not powered.
	 * adc_read will return error
	 */
	zassert_ok(gpio_emul_input_set(gpio_dev, GPIO_PG_EC_DSW_PWROK_PORT, 0),
		   NULL);

	zassert_equal(EC_ERROR_NOT_POWERED,
		      temp_sensor_read(
			      TEMP_SENSOR_ID(DT_NODELABEL(named_temp_charger)),
			      &temp),
		      NULL);
	zassert_equal(EC_ERROR_NOT_POWERED,
		      temp_sensor_read(
			      TEMP_SENSOR_ID(DT_NODELABEL(named_temp_ddr_soc)),
			      &temp),
		      NULL);
	zassert_equal(
		EC_ERROR_NOT_POWERED,
		temp_sensor_read(TEMP_SENSOR_ID(DT_NODELABEL(named_temp_fan)),
				 &temp),
		NULL);
	zassert_equal(EC_ERROR_NOT_POWERED,
		      temp_sensor_read(TEMP_SENSOR_ID(DT_NODELABEL(
					       named_temp_pp3300_regulator)),
				       &temp),
		      NULL);

	/* power ADC */
	zassert_ok(gpio_emul_input_set(gpio_dev, GPIO_PG_EC_DSW_PWROK_PORT, 1),
		   NULL);
}

/** Test error code when temp_sensor_read() is called power-good-pin low */
ZTEST_USER(temp_sensor, test_temp_sensor_pg_pin)
{
	const struct device *gpio_dev =
		DEVICE_DT_GET(DT_GPIO_CTLR(GPIO_EC_PG_PIN_TEMP_PATH, gpios));
	int temp;

	zassert_not_null(gpio_dev, "Cannot get GPIO device");

	/* ec_pg_pin_temp = 0 means temperature sensors are not powered. */
	zassert_ok(gpio_emul_input_set(gpio_dev, GPIO_EC_PG_PIN_TEMP_PORT, 0),
		   NULL);

	zassert_equal(EC_ERROR_NOT_POWERED,
		      temp_sensor_read(
			      TEMP_SENSOR_ID(DT_NODELABEL(named_temp_charger)),
			      &temp),
		      NULL);
	zassert_equal(EC_ERROR_NOT_POWERED,
		      temp_sensor_read(
			      TEMP_SENSOR_ID(DT_NODELABEL(named_temp_ddr_soc)),
			      &temp),
		      NULL);
	zassert_equal(
		EC_ERROR_NOT_POWERED,
		temp_sensor_read(TEMP_SENSOR_ID(DT_NODELABEL(named_temp_fan)),
				 &temp),
		NULL);
	zassert_equal(EC_ERROR_NOT_POWERED,
		      temp_sensor_read(TEMP_SENSOR_ID(DT_NODELABEL(
					       named_temp_pp3300_regulator)),
				       &temp),
		      NULL);

	/* power ADC */
	zassert_ok(gpio_emul_input_set(gpio_dev, GPIO_EC_PG_PIN_TEMP_PORT, 1),
		   NULL);
}

/** Simple ADC emulator custom function which always return error */
static int adc_error_func(const struct device *dev, unsigned int channel,
			  void *param, uint32_t *result)
{
	return -EINVAL;
}

/**
 * Set valid response only for ADC channel connected with tested sensor.
 * Check if temp_sensor_read() from tested sensor returns EC_SUCCESS and
 * valid temperature. Set invalid response on ADC channel for next test.
 */
static void check_valid_temperature(const struct device *adc_dev, int sensor)
{
	int temp;

	/* ADC channel of tested sensor return valid value */
	zassert_ok(adc_emul_const_value_set(adc_dev, temp_sensors[sensor].idx,
					    1000),
		   "adc_emul_const_value_set() failed (sensor %d)", sensor);
	zassert_equal(EC_SUCCESS, temp_sensor_read(sensor, &temp), NULL);
	zassert_within(
		temp, 273 + 50, 51,
		"Expected temperature in 0*C-100*C, got %d*C (sensor %d)",
		temp - 273, sensor);
	/* Return error on ADC channel of tested sensor */
	zassert_ok(adc_emul_value_func_set(adc_dev, temp_sensors[sensor].idx,
					   adc_error_func, NULL),
		   "adc_emul_value_func_set() failed (sensor %d)", sensor);
}

/** Test if temp_sensor_read() returns temperature on success */
ZTEST_USER(temp_sensor, test_temp_sensor_read)
{
	const struct device *adc_dev = DEVICE_DT_GET(ADC_DEVICE_NODE);
	int chan;

	zassert_not_null(adc_dev, "Cannot get ADC device");

	/* Return error on all ADC channels */
	for (chan = 0; chan < ADC_CHANNELS_NUM; chan++) {
		zassert_ok(adc_emul_value_func_set(adc_dev, chan,
						   adc_error_func, NULL),
			   "channel %d adc_emul_value_func_set() failed", chan);
	}

	check_valid_temperature(
		adc_dev, TEMP_SENSOR_ID(DT_NODELABEL(named_temp_charger)));
	check_valid_temperature(
		adc_dev, TEMP_SENSOR_ID(DT_NODELABEL(named_temp_ddr_soc)));
	check_valid_temperature(adc_dev,
				TEMP_SENSOR_ID(DT_NODELABEL(named_temp_fan)));
	check_valid_temperature(adc_dev, TEMP_SENSOR_ID(DT_NODELABEL(
						 named_temp_pp3300_regulator)));

	/* Return correct value on all ADC channels */
	for (chan = 0; chan < ADC_CHANNELS_NUM; chan++) {
		zassert_ok(adc_emul_const_value_set(adc_dev, chan, 1000),
			   "channel %d adc_emul_const_value_set() failed",
			   chan);
	}
}

static void *temp_sensor_setup(void)
{
	const struct device *dev =
		DEVICE_DT_GET(DT_GPIO_CTLR(GPIO_PG_EC_DSW_PWROK_PATH, gpios));
	const struct device *dev_pin =
		DEVICE_DT_GET(DT_GPIO_CTLR(GPIO_EC_PG_PIN_TEMP_PATH, gpios));

	zassert_not_null(dev, NULL);
	/* Before tests make sure that power pins are set. */
	zassert_ok(gpio_emul_input_set(dev, GPIO_PG_EC_DSW_PWROK_PORT, 1),
		   NULL);
	zassert_ok(gpio_emul_input_set(dev_pin, GPIO_EC_PG_PIN_TEMP_PORT, 1),
		   NULL);

	return NULL;
}

ZTEST_SUITE(temp_sensor, drivers_predicate_post_main, temp_sensor_setup, NULL,
	    NULL, NULL);
