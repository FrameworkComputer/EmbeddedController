/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* Emulator board-specific configuration */

#include "board.h"
#include "gpio.h"
#include "temp_sensor.h"

const struct gpio_info gpio_list[GPIO_COUNT] = {
	{"EC_INT", 0, 0, 0, 0},
	{"LID_OPEN", 0, 0, 0, 0},
	{"POWER_BUTTON_L", 0, 0, 0, 0},
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
