/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Temperature sensor module for Chrome EC */

#include "adc.h"
#include "adc_chip.h"
#include "common.h"
#include "hooks.h"

/* Initialize temperature reading to a sane value (27 C) */
static int last_val = C_TO_K(27);

static void chip_temp_sensor_poll(void)
{
#ifdef CONFIG_CMD_ECTEMP
	last_val = adc_read_channel(ADC_CH_EC_TEMP);
#endif
}
DECLARE_HOOK(HOOK_SECOND, chip_temp_sensor_poll, HOOK_PRIO_TEMP_SENSOR);

int chip_temp_sensor_get_val(int idx, int *temp_ptr)
{
	if (last_val == ADC_READ_ERROR)
		return EC_ERROR_UNKNOWN;

	*temp_ptr = last_val;

	return EC_SUCCESS;
}
