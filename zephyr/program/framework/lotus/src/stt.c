/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Support code for STT temperature reporting */

#include "amd_stt.h"
#include "chipset.h"
#include "temp_sensor/f75303.h"
#include "temp_sensor/pct2075.h"
#include "temp_sensor/temp_sensor.h"

int board_get_soc_temp_mk(int *temp_mk)
{
	if (chipset_in_state(CHIPSET_STATE_HARD_OFF))
		return EC_ERROR_NOT_POWERED;

	return f75303_get_val_mk(F75303_SENSOR_ID(DT_NODELABEL(apu_f75303)),
				 temp_mk);
}

int board_get_ambient_temp_mk(int *temp_mk)
{
	if (chipset_in_state(CHIPSET_STATE_HARD_OFF))
		return EC_ERROR_NOT_POWERED;

	return f75303_get_val_mk(
		F75303_SENSOR_ID(DT_NODELABEL(ambient_f75303)), temp_mk);
}

#ifdef CONFIG_PLATFORM_EC_GPU
int board_get_gpu_temp_mk(int *temp_mk)
{
	if (chipset_in_state(CHIPSET_STATE_HARD_OFF))
		return EC_ERROR_NOT_POWERED;

	return f75303_get_val_mk(
		F75303_SENSOR_ID(DT_NODELABEL(gpu_vr_f75303)), temp_mk);
}
#endif
