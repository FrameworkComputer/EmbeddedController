/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "battery.h"
#include "charger_profile_override.h"
#include "power.h"

#include <zephyr/drivers/adc/adc_emul.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#define DEFAULT_CURRENT 5000
static void set_adc_emul_read_voltage(int voltage, const struct device *adc_dev,
				      uint8_t channel_id)
{
	zassert_ok(adc_emul_const_value_set(adc_dev, channel_id, voltage));
}

static void wait_heat_stable(struct charge_state_data *curr)
{
	for (int i = 0; i < 5; i++) {
		curr->requested_current = DEFAULT_CURRENT;
		zassert_ok(charger_profile_override(curr));
	}
}

static void test_table(uint16_t batt, uint16_t chgv1, uint16_t lcdv1,
		       uint16_t chgv2, uint16_t lcdv2, uint16_t current,
		       enum power_state power)
{
	const struct device *adc_dev = DEVICE_DT_GET(DT_NODELABEL(adc0));
	uint8_t charger_adc_channel =
		DT_IO_CHANNELS_INPUT(DT_NODELABEL(adc_charger));
	uint8_t lcd_adc_channel =
		DT_IO_CHANNELS_INPUT(DT_NODELABEL(adc_temp_sensor_1));
	struct charge_state_data curr;

	curr.batt.flags = batt;
	power_set_state(power);
	set_adc_emul_read_voltage(chgv1, adc_dev, charger_adc_channel);
	set_adc_emul_read_voltage(lcdv1, adc_dev, lcd_adc_channel);
	wait_heat_stable(&curr);
	set_adc_emul_read_voltage(chgv2, adc_dev, charger_adc_channel);
	set_adc_emul_read_voltage(lcdv2, adc_dev, lcd_adc_channel);
	wait_heat_stable(&curr);
	zassert_equal(curr.requested_current, current);
}

ZTEST(temp_current, test_current_limit_in_each_zone)
{
	int battflag[2] = { BATT_FLAG_RESPONSIVE,
			    BATT_FLAG_RESPONSIVE | BATT_FLAG_BAD_TEMPERATURE };
	uint16_t current_table[] = { 5000, 2500, 1800, 1000, 0 };

	struct {
		uint16_t batt, chgv1, lcdv1, chgv2, lcdv2, current;
		enum power_state power;
	} testdata[] = {
		{ battflag[1], 411, 606, 411, 606, current_table[4], POWER_S5 },
		{ battflag[0], 209, 606, 209, 606, current_table[4], POWER_S5 },
		{ battflag[1], 446, 449, 446, 449, current_table[4], POWER_S5 },
		{ battflag[0], 209, 606, 209, 606, current_table[4], POWER_S5 },
		{ battflag[1], 411, 1219, 411, 1219, current_table[4],
		  POWER_S0 },
		{ battflag[0], 209, 1219, 209, 1219, current_table[4],
		  POWER_S0 },
		{ battflag[1], 446, 0, 446, 0, current_table[4], POWER_S0 },
		{ battflag[0], 209, 0, 209, 0, current_table[4], POWER_S0 },
		{ battflag[1], 411, 606, 411, 606, current_table[4], POWER_S0 },
		{ battflag[0], 209, 606, 209, 606, current_table[4], POWER_S0 },
		{ battflag[0], 446, 446, 418, 446, current_table[0], POWER_S0 },
		{ battflag[0], 418, 446, 397, 446, current_table[1], POWER_S0 },
		{ battflag[0], 397, 446, 384, 446, current_table[2], POWER_S0 },
		{ battflag[0], 384, 446, 343, 446, current_table[3], POWER_S0 },
		{ battflag[0], 257, 446, 209, 446, current_table[4], POWER_S0 },
		{ battflag[0], 209, 446, 257, 446, current_table[3], POWER_S0 },
		{ battflag[0], 376, 446, 411, 446, current_table[2], POWER_S0 },
		{ battflag[0], 411, 446, 439, 446, current_table[1], POWER_S0 },
		{ battflag[0], 439, 446, 483, 446, current_table[0], POWER_S0 },
	};
	for (int i = 0; i < ARRAY_SIZE(testdata); i++) {
		test_table(testdata[i].batt, testdata[i].chgv1,
			   testdata[i].lcdv1, testdata[i].chgv2,
			   testdata[i].lcdv2, testdata[i].current,
			   testdata[i].power);
	}
}

ZTEST_SUITE(temp_current, NULL, NULL, NULL, NULL, NULL);
