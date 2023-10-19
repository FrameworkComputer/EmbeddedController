/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "driver/temp_sensor/pct2075.h"
#include "emul/emul_pct2075.h"
#include "math_util.h"
#include "temp_sensor.h"
#include "temp_sensor/temp_sensor.h"
#include "test/drivers/test_state.h"
#include "test/drivers/utils.h"
#include "timer.h"

#include <math.h>

#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/adc/adc_emul.h>
#include <zephyr/drivers/emul.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#define GPIO_PG_EC_DSW_PWROK_PATH NAMED_GPIOS_GPIO_NODE(pg_ec_dsw_pwrok)
#define GPIO_PG_EC_DSW_PWROK_PORT DT_GPIO_PIN(GPIO_PG_EC_DSW_PWROK_PATH, gpios)

#define GPIO_EC_PG_PIN_TEMP_PATH NAMED_GPIOS_GPIO_NODE(ec_pg_pin_temp)
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
	zassert_equal(
		EC_ERROR_NOT_POWERED,
		temp_sensor_read(TEMP_SENSOR_ID(DT_NODELABEL(named_pct2075)),
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
	zassert_equal(EC_SUCCESS, temp_sensor_read(sensor, &temp));
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

/** Test if temp_sensor_read() returns temperature on success for PCT2075 */
ZTEST_USER(temp_sensor, test_temp_sensor_pct2075)
{
	int temp;
	const struct emul *dev = EMUL_DT_GET(DT_NODELABEL(pct2075_emul));
	int mk[] = {
		MILLI_CELSIUS_TO_MILLI_KELVIN(127000),
		MILLI_CELSIUS_TO_MILLI_KELVIN(126850),
		MILLI_CELSIUS_TO_MILLI_KELVIN(125),
		MILLI_CELSIUS_TO_MILLI_KELVIN(0),
		MILLI_CELSIUS_TO_MILLI_KELVIN(-125),
		MILLI_CELSIUS_TO_MILLI_KELVIN(-54875),
		MILLI_CELSIUS_TO_MILLI_KELVIN(-55000),
	};

	for (int i = 0; i < ARRAY_SIZE(mk); i++) {
		pct2075_emul_set_temp(dev, mk[i]);
		/* Highly dependent on current implementation. The sensor
		 * update temperature in the 1 second periodic hook, so
		 * we need to wait for it.
		 */
		msleep(1100);
		zassert_equal(EC_SUCCESS,
			      temp_sensor_read(TEMP_SENSOR_ID(DT_NODELABEL(
						       named_pct2075)),
					       &temp));
		zassert_equal(MILLI_KELVIN_TO_KELVIN(mk[i]), temp);
	}
}

/** Test if temperature is not updated on I2C read fail.
 *  The test highly dependent on current implementation - temp_sensor_read
 *  doesn't return an error on the i2c read fail, which can/should be changed
 *  in the future.
 */
ZTEST_USER(temp_sensor, test_temp_sensor_pct2075_fail)
{
	const struct emul *dev = EMUL_DT_GET(DT_NODELABEL(pct2075_emul));
	struct pct2075_data *data = (struct pct2075_data *)dev->data;
	int mk1 = 373000, mk2 = 273000;
	int temp;

	/* Set initial temperature */
	pct2075_emul_set_temp(dev, mk1);
	msleep(1100);

	zassert_equal(EC_SUCCESS, temp_sensor_read(TEMP_SENSOR_ID(DT_NODELABEL(
							   named_pct2075)),
						   &temp));
	/* Make sure the temperature is read correctly */
	zassert_equal(MILLI_KELVIN_TO_KELVIN(mk1), temp);

	/* Set I2C fail on the temperature register */
	i2c_common_emul_set_read_fail_reg(&data->common, PCT2075_REG_TEMP);
	pct2075_emul_set_temp(dev, mk2);
	/* Wait for potential update */
	msleep(1100);

	/* Make sure the temperature is not changed */
	zassert_equal(EC_SUCCESS, temp_sensor_read(TEMP_SENSOR_ID(DT_NODELABEL(
							   named_pct2075)),
						   &temp));
	zassert_equal(MILLI_KELVIN_TO_KELVIN(mk1), temp);

	/* Restore I2C */
	i2c_common_emul_set_read_fail_reg(&data->common,
					  I2C_COMMON_EMUL_NO_FAIL_REG);
	/* Wait for update */
	msleep(1100);
	/* Make sure the temperature is updated */
	zassert_equal(EC_SUCCESS, temp_sensor_read(TEMP_SENSOR_ID(DT_NODELABEL(
							   named_pct2075)),
						   &temp));
	zassert_equal(MILLI_KELVIN_TO_KELVIN(mk2), temp);
}

/*
 * Test we see reasonable prints from temperature sensors on the console
 */
ZTEST_USER(temp_sensor, test_temps_print_good)
{
	check_console_cmd("temps", "K (= ", EC_SUCCESS, __FILE__, __LINE__);
}

/*
 * Test we see error returns for an unpowered sensor
 */
ZTEST_USER(temp_sensor, test_temps_print_unpowered)
{
	const struct device *gpio_dev =
		DEVICE_DT_GET(DT_GPIO_CTLR(GPIO_EC_PG_PIN_TEMP_PATH, gpios));

	zassert_not_null(gpio_dev, "Cannot get GPIO device");

	/* ec_pg_pin_temp = 0 means temperature sensors are not powered. */
	zassert_ok(gpio_emul_input_set(gpio_dev, GPIO_EC_PG_PIN_TEMP_PORT, 0),
		   NULL);

	check_console_cmd("temps", "Not powered", EC_ERROR_NOT_POWERED,
			  __FILE__, __LINE__);
}

/* Test that we report temp sensor info up to the AP if asked */
ZTEST_USER(temp_sensor, test_temp_get_info_good)
{
	struct ec_params_temp_sensor_get_info params = {
		.id = 0,
	};
	struct ec_response_temp_sensor_get_info response;

	zassert_ok(ec_cmd_temp_sensor_get_info(NULL, &params, &response));
	zassert_equal(response.sensor_type, TEMP_SENSOR_TYPE_BOARD);
}

ZTEST_USER(temp_sensor, test_temp_get_info_failure)
{
	struct ec_params_temp_sensor_get_info params = {
		.id = TEMP_SENSOR_COUNT,
	};
	struct ec_response_temp_sensor_get_info response;

	zassert_equal(ec_cmd_temp_sensor_get_info(NULL, &params, &response),
		      EC_RES_ERROR);
}

static void temp_sensor_after(void *fixture)
{
	/* Clean up any PGOOD pin changes */
	const struct device *dev =
		DEVICE_DT_GET(DT_GPIO_CTLR(GPIO_PG_EC_DSW_PWROK_PATH, gpios));
	const struct device *dev_pin =
		DEVICE_DT_GET(DT_GPIO_CTLR(GPIO_EC_PG_PIN_TEMP_PATH, gpios));

	zassert_not_null(dev, NULL);
	zassert_ok(gpio_emul_input_set(dev, GPIO_PG_EC_DSW_PWROK_PORT, 1),
		   NULL);
	zassert_ok(gpio_emul_input_set(dev_pin, GPIO_EC_PG_PIN_TEMP_PORT, 1),
		   NULL);
}

static void *temp_sensor_setup(void)
{
	const struct device *dev =
		DEVICE_DT_GET(DT_GPIO_CTLR(GPIO_PG_EC_DSW_PWROK_PATH, gpios));
	const struct device *dev_pin =
		DEVICE_DT_GET(DT_GPIO_CTLR(GPIO_EC_PG_PIN_TEMP_PATH, gpios));
	const struct emul *pct2075_dev =
		EMUL_DT_GET(DT_NODELABEL(pct2075_emul));
	struct pct2075_data *pct2075_data =
		(struct pct2075_data *)pct2075_dev->data;

	zassert_not_null(dev, NULL);
	/* Before tests make sure that power pins are set. */
	zassert_ok(gpio_emul_input_set(dev, GPIO_PG_EC_DSW_PWROK_PORT, 1),
		   NULL);
	zassert_ok(gpio_emul_input_set(dev_pin, GPIO_EC_PG_PIN_TEMP_PORT, 1),
		   NULL);

	i2c_common_emul_set_read_fail_reg(&pct2075_data->common,
					  I2C_COMMON_EMUL_NO_FAIL_REG);

	return NULL;
}

ZTEST_SUITE(temp_sensor, drivers_predicate_post_main, temp_sensor_setup, NULL,
	    temp_sensor_after, NULL);
