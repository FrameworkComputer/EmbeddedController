/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Temperature sensor module for Chrome EC */

#include "adc.h"
#include "config.h"
#include "hooks.h"
#include "lm4_adc.h"
#include "temp_sensor.h"

static int last_val;

int chip_temp_sensor_poll(void)
{
	last_val = adc_read_channel(ADC_CH_EC_TEMP);

	return EC_SUCCESS;
}

int chip_temp_sensor_get_val(int idx, int *temp_ptr)
{
	if (last_val == ADC_READ_ERROR)
		return EC_ERROR_UNKNOWN;

	*temp_ptr = last_val;

	return EC_SUCCESS;
}

static int chip_temp_sensor_init(void)
{
	/* Initialize temperature reading to a sane value. */
	last_val = 300; /* 27 C */
	return EC_SUCCESS;
}
DECLARE_HOOK(HOOK_INIT, chip_temp_sensor_init, HOOK_PRIO_DEFAULT);
