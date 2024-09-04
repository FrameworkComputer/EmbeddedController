/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "charge_state.h"
#include "charger.h"
#include "charger_profile_override.h"
#include "common.h"
#include "config.h"
#include "hooks.h"
#include "temp_sensor.h"
#include "temp_sensor/temp_sensor.h"
#include "util.h"

#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/adc/adc_emul.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#define ADC_DEVICE_NODE DT_NODELABEL(adc0)
#define CHARGER_TEMP TEMP_SENSOR_ID(DT_NODELABEL(temp_charger))
#define ORIGINAL_CURRENT 2400
#define LOW_CURRENT 365

struct charge_state_data curr;
static int fake_voltage;
int count;

/* Limit charging current table : 2400/1400/365
 * note this should be in descending order.
 */

struct current_table_struct {
	int temperature;
	int current;
};

static const struct current_table_struct current_table[] = {
	{ 0, 2400 },
	{ 55, 1400 },
	{ 57, 365 },
};

#define CURRENT_LEVELS ARRAY_SIZE(current_table)

static void heating_device(void)
{
	for (int i = 0; i < 55; i++) {
		hook_notify(HOOK_SECOND);
		curr.requested_current = ORIGINAL_CURRENT;
		charger_profile_override(&curr);
	}
}

static void cool_down_device(void)
{
	for (int i = 0; i < 55; i++) {
		hook_notify(HOOK_SECOND);
		curr.requested_current = LOW_CURRENT;
		charger_profile_override(&curr);
	}
}

int setup_faketemp(int fake_voltage)
{
	const struct device *adc_dev = DEVICE_DT_GET(ADC_DEVICE_NODE);
	const uint8_t channel_id =
		DT_IO_CHANNELS_INPUT(DT_NODELABEL(adc_charger));
	int emul_temp;

	emul_temp = adc_emul_const_value_set(adc_dev, channel_id, fake_voltage);
	return emul_temp;
}

ZTEST(temp_skitty, test_decrease_current_level)
{
	fake_voltage = 350;
	curr.batt.flags |= BATT_FLAG_RESPONSIVE;
	count = 0;

	setup_faketemp(fake_voltage);
	heating_device();
	zassert_equal(curr.requested_current, current_table[count].current);
	for (int uptime_time = 0; uptime_time < 13; uptime_time++) {
		hook_notify(HOOK_SECOND);
		curr.requested_current = ORIGINAL_CURRENT;
		charger_profile_override(&curr);
		if (uptime_time % 6 == 0) {
			if (curr.requested_current != ORIGINAL_CURRENT) {
				count++;
				zassert_equal(curr.requested_current,
					      current_table[count].current);
			}
		}
	}
}

ZTEST(temp_skitty, test_increase_current)
{
	fake_voltage = 400;
	curr.batt.flags |= BATT_FLAG_RESPONSIVE;
	count = 2;

	setup_faketemp(fake_voltage);
	cool_down_device();
	zassert_equal(curr.requested_current, current_table[count].current);

	for (int uptime_time = 0; uptime_time < 60; uptime_time++) {
		hook_notify(HOOK_SECOND);
		curr.requested_current = ORIGINAL_CURRENT;
		charger_profile_override(&curr);
		if (uptime_time % 6 == 0) {
			if (curr.requested_current != ORIGINAL_CURRENT) {
				count--;
				zassert_equal(curr.requested_current,
					      current_table[count].current);
			}
		}
	}
}

ZTEST(temp_skitty, test_battery_no_response)
{
	int rv;

	curr.batt.flags &= ~BATT_FLAG_RESPONSIVE;
	rv = charger_profile_override(&curr);
	zassert_equal(rv, 0);
}

ZTEST(temp_skitty, test_charger_profile_override_get_param)
{
	int rv;

	rv = charger_profile_override_get_param(0, 0);

	zassert_equal(rv, 3);
}

ZTEST(temp_skitty, test_charger_profile_override_set_param)
{
	int rv;

	rv = charger_profile_override_set_param(0, 0);

	zassert_equal(rv, 3);
}

ZTEST_SUITE(temp_skitty, NULL, NULL, NULL, NULL, NULL);
