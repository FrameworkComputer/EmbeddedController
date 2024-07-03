/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "chipset.h"
#include "test_vcmp_sensor.h"

#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/fff.h>
#include <zephyr/input/input.h>
#include <zephyr/shell/shell_dummy.h>
#include <zephyr/sys/util.h>
#include <zephyr/ztest.h>

#include <dt-bindings/kbd.h>

DEFINE_FFF_GLOBALS;

int test_reinit(void);

FAKE_VALUE_FUNC(int, chipset_in_state, int);
FAKE_VALUE_FUNC(int, test_sensor_trigger_set, const struct device *,
		const struct sensor_trigger *, sensor_trigger_handler_t);

static bool sensor_enabled;
static int sensor_threshold_value;
static bool sensor_threshold_polarity;
static int sensor_attr_fail_counter;
static int test_sensor_attr_set(const struct device *dev,
				enum sensor_channel chan,
				enum sensor_attribute attr,
				const struct sensor_value *val)
{
	if (sensor_attr_fail_counter == 0) {
		return -1;
	} else if (sensor_attr_fail_counter > 0) {
		sensor_attr_fail_counter--;
	}

	if (chan == SENSOR_CHAN_VOLTAGE && attr == SENSOR_ATTR_ALERT) {
		sensor_enabled = val->val1;
	} else if (chan == SENSOR_CHAN_VOLTAGE &&
		   attr == SENSOR_ATTR_UPPER_VOLTAGE_THRESH) {
		sensor_threshold_polarity = true;
		sensor_threshold_value = val->val1;
	} else if (chan == SENSOR_CHAN_VOLTAGE &&
		   attr == SENSOR_ATTR_LOWER_VOLTAGE_THRESH) {
		sensor_threshold_polarity = false;
		sensor_threshold_value = val->val1;
	} else {
		return -1;
	}
	return 0;
}

sensor_trigger_handler_t callback;

static const struct sensor_driver_api test_sensor_api = {
	.attr_set = test_sensor_attr_set,
	.trigger_set = test_sensor_trigger_set,
};

DEVICE_DT_DEFINE(DT_INST(0, test_sensor), NULL, NULL, NULL, NULL, PRE_KERNEL_1,
		 CONFIG_KERNEL_INIT_PRIORITY_DEVICE, &test_sensor_api);

ZTEST(prochot_vcmp, test_prochot_vcmp_cb_fail)
{
	/* initial state */
	zassert_equal(sensor_threshold_value, 800);
	zassert_equal(sensor_threshold_polarity, true);
	zassert_equal(sensor_enabled, true);

	sensor_attr_fail_counter = 0;

	callback(NULL, NULL);

	/* no changes */
	zassert_equal(sensor_threshold_value, 800);
	zassert_equal(sensor_threshold_polarity, true);
	zassert_equal(sensor_enabled, true);

	sensor_enabled = true;
	sensor_attr_fail_counter = 1;

	callback(NULL, NULL);

	/* vcmp left disabled */
	zassert_equal(sensor_threshold_value, 800);
	zassert_equal(sensor_threshold_polarity, true);
	zassert_equal(sensor_enabled, false);

	sensor_enabled = true;
	sensor_attr_fail_counter = 2;

	callback(NULL, NULL);

	/* threshold changed but vcmp disabled */
	zassert_equal(sensor_threshold_value, 500);
	zassert_equal(sensor_threshold_polarity, false);
	zassert_equal(sensor_enabled, false);
}

ZTEST(prochot_vcmp, test_prochot_vcmp)
{
	const struct shell *shell_zephyr = shell_backend_dummy_get_ptr();
	const char *outbuffer;
	size_t buffer_size;

	/* Give the backend time to initialize */
	k_sleep(K_MSEC(100));
	shell_backend_dummy_clear_output(shell_zephyr);

	chipset_in_state_fake.return_val = CHIPSET_STATE_NOT_ON;

	callback(NULL, NULL); /* arguments not used */

	outbuffer = shell_backend_dummy_get_output(shell_zephyr, &buffer_size);
	zassert_equal(buffer_size, 0, "unexpected data on the log buffer");
	zassert_equal(sensor_threshold_value, 500);
	zassert_equal(sensor_threshold_polarity, false);
	zassert_equal(sensor_enabled, true);
	shell_backend_dummy_clear_output(shell_zephyr);

	callback(NULL, NULL);

	outbuffer = shell_backend_dummy_get_output(shell_zephyr, &buffer_size);
	zassert_equal(buffer_size, 0, "unexpected data on the log buffer");
	zassert_equal(sensor_threshold_value, 800);
	zassert_equal(sensor_threshold_polarity, true);
	zassert_equal(sensor_enabled, true);
	shell_backend_dummy_clear_output(shell_zephyr);

	chipset_in_state_fake.return_val = CHIPSET_STATE_ON;

	callback(NULL, NULL);

	outbuffer = shell_backend_dummy_get_output(shell_zephyr, &buffer_size);
	zassert_true(buffer_size > 0);
	zassert_not_null(strstr(outbuffer, "PROCHOT state: deasserted"));
	zassert_equal(sensor_threshold_value, 500);
	zassert_equal(sensor_threshold_polarity, false);
	zassert_equal(sensor_enabled, true);
	shell_backend_dummy_clear_output(shell_zephyr);

	callback(NULL, NULL);

	outbuffer = shell_backend_dummy_get_output(shell_zephyr, &buffer_size);
	zassert_true(buffer_size > 0);
	zassert_not_null(strstr(outbuffer, "PROCHOT state: asserted"));
	zassert_equal(sensor_threshold_value, 800);
	zassert_equal(sensor_threshold_polarity, true);
	zassert_equal(sensor_enabled, true);
	shell_backend_dummy_clear_output(shell_zephyr);
}

static void reset(void *fixture)
{
	ARG_UNUSED(fixture);

	RESET_FAKE(chipset_in_state);
	RESET_FAKE(test_sensor_trigger_set);

	sensor_enabled = false;
	sensor_threshold_value = -1;
	sensor_threshold_polarity = false;
	sensor_attr_fail_counter = -1;

	test_reinit();

	/* test initial settings before resetting */
	zassert_equal(test_sensor_trigger_set_fake.call_count, 1);
	callback = test_sensor_trigger_set_fake.arg2_val;

	zassert_equal(sensor_enabled, true);
	zassert_equal(sensor_threshold_value, 800);
	zassert_equal(sensor_threshold_polarity, true);
}

ZTEST_SUITE(prochot_vcmp, NULL, NULL, reset, NULL, NULL);
