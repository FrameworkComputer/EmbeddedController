/* Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Temperature sensor module for Chrome EC */

#include "adc.h"
#include "board.h"
#include "temp_sensor.h"

int chip_temp_sensor_read(const struct temp_sensor_t* sensor)
{
	return adc_read_channel(ADC_CH_EC_TEMP);
}

int chip_temp_sensor_init(void)
{
	return EC_SUCCESS;
}
