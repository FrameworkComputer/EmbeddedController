/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* EC_ADC driver for Chrome EC */

#include "adc.h"
#include "common.h"
#include "console.h"
#include "ec_adc.h"
#include "thermistor.h"
#include "util.h"

/* Get temperature from requested sensor */
static int get_temp(int idx, int *temp_ptr)
{
	int temp_raw = 0;

	/* Read 10-bit ADC result */
	temp_raw = adc_read_channel(idx);

	if (temp_raw == ADC_READ_ERROR)
		return EC_ERROR_UNKNOWN;

	/* TODO : Need modification here if the result is not 10-bit */

	/* If there is no thermistor calculation function.
	 *  1. Add adjusting function like thermistor_ncp15wb.c
	 *  2. Place function here with ifdef
	 *  3. define it on board.h
	 */
#ifdef CONFIG_THERMISTOR_NCP15WB
	*temp_ptr = ncp15wb_calculate_temp((uint16_t) temp_raw);
#else
#error "Unknown thermistor for ec_adc"
	return EC_ERROR_UNKNOWN;
#endif

	return EC_SUCCESS;
}

int ec_adc_get_val(int idx, int *temp_ptr)
{
	int ret;
	int temp_c;

	if(idx < 0 || idx >= ADC_CH_COUNT)
		return EC_ERROR_INVAL;

	ret = get_temp(idx, &temp_c);
	if (ret == EC_SUCCESS)
		*temp_ptr = C_TO_K(temp_c);

	return ret;
}
