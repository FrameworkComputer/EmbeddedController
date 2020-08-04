/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <adc.h>
#include <common.h>
#include <console.h>
#include <gpio.h>
#include <timer.h>
#include <util.h>

/**
 * Conversion based on following table:
 *
 * ID | Rp   | Rd   | Voltage
 *    | kOhm | kOhm | mV
 * ---+------+------+--------
 *  0 | 51.1 |  2.2 | 136.2
 *  1 | 51.1 | 6.81 | 388.1
 *  2 | 51.1 |   11 | 584.5
 *  3 | 57.6 |   18 | 785.7
 *  4 | 51.1 |   22 | 993.2
 *  5 | 51.1 |   30 | 1220.7
 *  6 | 51.1 | 39.2 | 1432.6
 *  7 |   56 |   56 | 1650.0
 *  8 |   47 | 61.9 | 1875.8
 *  9 |   47 | 80.6 | 2084.5
 * 10 |   56 |  124 | 2273.3
 * 11 | 51.1 |  150 | 2461.5
 * 12 |   47 |  200 | 2672.1
 * 13 |   47 |  330 | 2888.6
 * 14 |   47 |  680 | 3086.7
 */
const static int voltage_map[] = {
	136,
	388,
	584,
	785,
	993,
	1220,
	1432,
	1650,
	1875,
	2084,
	2273,
	2461,
	2672,
	2888,
	3086,
};

const int threshold_mv = 100;

/**
 * Convert ADC value to board id using the voltage table above.
 *
 * @param ch	ADC channel to read, usually ADC_BOARD_ID_0 or ADC_BOARD_ID_1.
 *
 * @return	a non-negative board id, or negative value if error.
 */
static int adc_value_to_numeric_id(enum adc_channel ch)
{
	int mv;

	gpio_set_level(GPIO_EN_EC_ID_ODL, 0);
	/* Wait to allow cap charge */
	msleep(10);

	mv = adc_read_channel(ch);
	if (mv == ADC_READ_ERROR)
		mv = adc_read_channel(ch);

	gpio_set_level(GPIO_EN_EC_ID_ODL, 1);

	if (mv == ADC_READ_ERROR)
		return -EC_ERROR_UNKNOWN;

	for (int i = 0; i < ARRAY_SIZE(voltage_map); i++) {
		if (IN_RANGE(mv, voltage_map[i] - threshold_mv,
			     voltage_map[i] + threshold_mv))
			return i;
	}

	return -EC_ERROR_UNKNOWN;
}

int board_get_version(void)
{
	static int version = -1;

	if (version >= 0)
		return version;

	version = adc_value_to_numeric_id(ADC_BOARD_ID_0);
	if (version < 0) {
		ccprints("WARNING: failed to read ADC_BOARD_ID_0");
		ccprints("Assuming board id = 0");

		version = 0;
	}

	return version;
}
