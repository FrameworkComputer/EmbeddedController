/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "ec_commands.h"
#include "host_command.h"
#include "port80.h"
#include "temp_sensor.h"
#include "temp_sensor/temp_sensor.h"
#include "test/drivers/test_state.h"
#include "zephyr/kernel.h"

#include <zephyr/drivers/adc/adc_emul.h>
#include <zephyr/drivers/espi_emul.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/fff.h>
#include <zephyr/logging/log.h>
#include <zephyr/ztest.h>
#include <zephyr/ztest_assert.h>

LOG_MODULE_REGISTER(test_shim_espi, LOG_LEVEL_DBG);

const struct device *const ESPI_DEV = DEVICE_DT_GET(DT_NODELABEL(espi0));

FAKE_VOID_FUNC(port_80_write, int);

ZTEST_USER(espi_shim, test_get_protocol_info)
{
	struct ec_response_get_protocol_info response;
	struct host_cmd_handler_args args = BUILD_HOST_COMMAND_RESPONSE(
		EC_CMD_GET_PROTOCOL_INFO, 0, response);

	zassert_ok(host_command_process(&args));
}

ZTEST_USER(espi_shim, test_port80)
{
	RESET_FAKE(port_80_write);

	emul_espi_host_port80_write(ESPI_DEV, 0x55aa);

	zassert_equal(port_80_write_fake.call_count, 1,
		      "Port 80 writes %d, expected %d",
		      port_80_write_fake.call_count, 1);
	zassert_equal(port_80_write_fake.arg0_val, 0x55aa);
}

#ifdef CONFIG_PLATFORM_EC_TEMP_SENSOR
#define ADC_DEVICE_NODE DT_NODELABEL(adc0)
#define ADC_CHANNELS_NUM DT_PROP(DT_NODELABEL(adc0), nchannels)

static void seed_temp_sensors(void)
{
	const struct device *adc_dev = DEVICE_DT_GET(DT_NODELABEL(adc0));
	const struct gpio_dt_spec *temp_power_gpio =
		GPIO_DT_FROM_NODELABEL(gpio_ec_pg_pin_temp);
	const struct gpio_dt_spec *ap_power_gpio =
		GPIO_DT_FROM_NODELABEL(gpio_pg_ec_dsw_pwrok);

	zassert_not_null(temp_power_gpio->port, "Cannot get GPIO device");
	zassert_not_null(ap_power_gpio->port, "Cannot get GPIO device");
	zassert_not_null(adc_dev, "Cannot get ADC device");

	/* Simulate power to the AP and temp sensor. */
	zassert_ok(gpio_emul_input_set(ap_power_gpio->port, ap_power_gpio->pin,
				       1));
	zassert_ok(gpio_emul_input_set(temp_power_gpio->port,
				       temp_power_gpio->pin, 1));

	for (int chan = 0; chan < ADC_CHANNELS_NUM; chan++) {
		zassert_ok(adc_emul_const_value_set(adc_dev, chan, 1000),
			   "channel %d adc_emul_const_value_set() failed",
			   chan);
	}
}

ZTEST_USER(espi_shim, test_memmap_temp_sensor)
{
	uint8_t *host_shm;
	int temp;

	/* Get the location of the memory buffer that mirrors the
	 * EC shared memory region.
	 */
	host_shm = (uint8_t *)emul_espi_host_get_acpi_shm(ESPI_DEV);

	seed_temp_sensors();

	/* Allow background thread to update shared memory. */
	k_sleep(K_SECONDS(2));

	temp_sensor_read(0, &temp);
	LOG_INF("Temp sensor raw value %d\n", temp);
	zassert_within(temp, 273 + 50, 51,
		       "Expected temperature in 0*C-100*C, got %d*C (sensor 0)",
		       temp - 273);

	/* Host temperature is scaled down by EC_TEMP_SENSOR_OFFSET. */
	zassert_equal(host_shm[EC_MEMMAP_TEMP_SENSOR],
		      temp - EC_TEMP_SENSOR_OFFSET,
		      "Expected memmap temp %d, got %d",
		      temp - EC_TEMP_SENSOR_OFFSET,
		      host_shm[EC_MEMMAP_TEMP_SENSOR]);
}
#else /* CONFIG_PLATFORM_EC_TEMP_SENSOR */
ZTEST_USER(espi_shim, test_memmap_no_temp_sensor)
{
	uint8_t *host_shm;

	/* Get the location of the memory buffer that mirrors the
	 * EC shared memory region.
	 */
	host_shm = (uint8_t *)emul_espi_host_get_acpi_shm(ESPI_DEV);

	zassert_equal(host_shm[EC_MEMMAP_TEMP_SENSOR],
		      EC_TEMP_SENSOR_NOT_PRESENT);
}
#endif

ZTEST_SUITE(espi_shim, drivers_predicate_post_main, NULL, NULL, NULL, NULL);
