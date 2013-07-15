/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* Emulator board-specific configuration */

#include "gpio.h"
#include "temp_sensor.h"

#define MOCK_GPIO(x) {#x, 0, 0, 0, 0}

const struct gpio_info gpio_list[GPIO_COUNT] = {
	MOCK_GPIO(EC_INT),
	MOCK_GPIO(LID_OPEN),
	MOCK_GPIO(POWER_BUTTON_L),
	MOCK_GPIO(WP),
	MOCK_GPIO(ENTERING_RW),
	MOCK_GPIO(AC_PRESENT),
};

static int dummy_temp_get_val(int idx, int *temp_ptr)
{
	*temp_ptr = 0;
	return EC_SUCCESS;
}

const struct temp_sensor_t temp_sensors[TEMP_SENSOR_COUNT] = {
	{"CPU", TEMP_SENSOR_TYPE_CPU, dummy_temp_get_val, 0, 3},
	{"Board", TEMP_SENSOR_TYPE_BOARD, dummy_temp_get_val, 0, 3},
	{"Case", TEMP_SENSOR_TYPE_CASE, dummy_temp_get_val, 0, 0},
};
