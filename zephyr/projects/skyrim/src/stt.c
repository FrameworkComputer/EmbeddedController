/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Support code for STT temperature reporting */

#include "chipset.h"
#include "temp_sensor/pct2075.h"
#include "temp_sensor/temp_sensor.h"

int board_get_soc_temp_mk(int *temp_mk)
{
	if (chipset_in_state(CHIPSET_STATE_HARD_OFF))
		return EC_ERROR_NOT_POWERED;

	return pct2075_get_val_mk(PCT2075_SOC, temp_mk);
}

int board_get_ambient_temp_mk(int *temp_mk)
{
	if (chipset_in_state(CHIPSET_STATE_HARD_OFF))
		return EC_ERROR_NOT_POWERED;

	return pct2075_get_val_mk(PCT2075_AMB, temp_mk);
}
