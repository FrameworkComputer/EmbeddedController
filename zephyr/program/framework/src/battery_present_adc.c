/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "adc.h"
#include "battery.h"
#include "board_adc.h"

static enum battery_present batt_pres_prev = BP_NOT_SURE;

enum battery_present battery_is_present(void)
{
	enum battery_present batt_pres;
	int mv;

	mv = adc_read_channel(ADC_VCIN1_BATT_TEMP);
	batt_pres = (mv < 2200 ? BP_YES : BP_NO);

	if (mv == ADC_READ_ERROR)
		return BP_NO;

	/*
	 * If the battery is present now and was present last time we checked,
	 * return early.
	 */
	if (batt_pres == BP_YES && batt_pres_prev == batt_pres)
		return batt_pres;

	batt_pres_prev = batt_pres;

	return batt_pres;
}
