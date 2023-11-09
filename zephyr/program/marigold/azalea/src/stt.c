/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Support code for STT temperature reporting */

#include "chipset.h"
#include "temp_sensor/f75303.h"
#include "temp_sensor/pct2075.h"
#include "temp_sensor/temp_sensor.h"

int board_get_soc_temp_mk(int *temp_mk)
{
	if (chipset_in_state(CHIPSET_STATE_HARD_OFF))
		return EC_ERROR_NOT_POWERED;

	return f75303_get_val_mk(F75303_SENSOR_ID(DT_NODELABEL(cpu_f75303)),
				 temp_mk);
}

int board_get_ambient_temp_mk(int *temp_mk)
{
	if (chipset_in_state(CHIPSET_STATE_HARD_OFF))
		return EC_ERROR_NOT_POWERED;

	return f75303_get_val_mk(
		F75303_SENSOR_ID(DT_NODELABEL(ddr_f75303)), temp_mk);
}
